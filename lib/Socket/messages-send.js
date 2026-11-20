import NodeCache from '@cacheable/node-cache';
import { Boom } from '@hapi/boom';
import { proto } from '../../WAProto/index.js';
import { DEFAULT_CACHE_TTLS, WA_DEFAULT_EPHEMERAL } from '../Defaults/index.js';
import { aggregateMessageKeysNotFromMe, assertMediaContent, assertMeId, bindWaitForEvent, decryptMediaRetryData, DEF_MEDIA_HOST, downloadMediaMessage, encodeNewsletterMessage, encodeSignedDeviceIdentity, encodeWAMessage, encryptMediaRetryRequest, extractDeviceJids, generateMessageIDV2, generateParticipantHashV2, generateWAMessage, getStatusCodeForMediaRetry, getUrlFromDirectPath, getWAUploadToServer, MessageRetryManager, normalizeMessageContent, parseAndInjectE2ESessions, unixTimestampSeconds } from '../Utils/index.js';
import { getUrlInfo } from '../Utils/link-preview.js';
import { makeKeyedMutex, makeMutex } from '../Utils/make-mutex.js';
import { createArkanaHelpers } from '../Utils/arkana-helpers.js';
import { generateWAMessageFromContent, getContentType, prepareWAMessageMedia } from '../Utils/messages.js';
import { getMessageReportingToken, shouldIncludeReportingToken } from '../Utils/reporting-utils.js';
import { buildMergedTcTokenIndexWrite, isTcTokenExpired, resolveIssuanceJid, resolveTcTokenJid, shouldSendNewTcToken, storeTcTokensFromIqResult } from '../Utils/tc-token-utils.js';
import { areJidsSameUser, getBinaryNodeChild, getBinaryNodeChildren, isHostedLidUser, isHostedPnUser, isJidBot, isJidGroup, isJidMetaAI, isLidUser, isPnUser, jidDecode, jidEncode, jidNormalizedUser, PSA_WID, S_WHATSAPP_NET } from '../WABinary/index.js';
import { USyncQuery, USyncUser } from '../WAUSync/index.js';
import { makeNewsletterSocket } from './newsletter.js';
import { unwrapMessageContent } from './socket.js';

const MEDIA_MESSAGE_KEYS = ['imageMessage', 'videoMessage', 'audioMessage', 'stickerMessage', 'documentMessage'];
const CONTEXT_HOLDER_KEYS = [
    'conversation',
    'extendedTextMessage',
    'imageMessage',
    'videoMessage',
    'audioMessage',
    'documentMessage',
    'stickerMessage',
    'interactiveMessage',
    'albumMessage',
    'ptvMessage'
];

const sanitizeJid = (jid) => typeof jid === 'string'
    ? jid.replace(/:\d+@/, '@')
    : jid;
const wait = (ms) => new Promise(resolve => setTimeout(resolve, ms));

export const buildInteractiveBizNode = (content) => {
    const core = unwrapMessageContent(content);
    if (!core) {
        return;
    }
    const firstButton = core.interactiveMessage?.nativeFlowMessage?.buttons?.[0] ||
        core.interactiveMessage?.carouselMessage?.cards?.[0]?.nativeFlowMessage?.buttons?.[0];
    const flowName = firstButton?.name || (core.interactiveResponseMessage ? 'galaxy_message' : 'mpm');
    if (core.interactiveMessage || core.interactiveResponseMessage || core.buttonsMessage || core.templateMessage) {
        return {
            tag: 'biz',
            attrs: {},
            content: [
                {
                    tag: 'interactive',
                    attrs: { type: 'native_flow', v: '1' },
                    content: [{ tag: 'native_flow', attrs: { v: '2', name: flowName } }]
                }
            ]
        };
    }
    if (core.listMessage) {
        return {
            tag: 'biz',
            attrs: {},
            content: [{ tag: 'list', attrs: { type: 'product_list', v: '2' } }]
        };
    }
};

export const mergeAdditionalNodes = (existingNodes, interactiveNode) => {
    const baseNodes = Array.isArray(existingNodes) ? [...existingNodes] : [];
    if (interactiveNode && !baseNodes.some(node => node?.tag === 'biz')) {
        baseNodes.push(interactiveNode);
    }
    return baseNodes;
};

const createContextPatch = (options = {}) => {
    const patch = { ...(options.contextInfo || {}) };
    const badge = options.channelBadge;
    if (badge) {
        const info = typeof badge === 'object' ? badge : {};
        patch.isForwarded = true;
        patch.forwardingScore = 999;
        patch.forwardedNewsletterMessageInfo = {
            newsletterJid: info.newsletterJid || info.jid || '120363012345678901@newsletter',
            newsletterName: info.newsletterName || info.name || 'Arkana',
            ...(info.serverMessageId ? { serverMessageId: Number(info.serverMessageId) } : {})
        };
    }
    return Object.keys(patch).length ? patch : null;
};

export const applyContextPatch = (generated, options = {}) => {
    const patch = createContextPatch(options);
    if (!patch || !generated?.message) {
        return generated;
    }
    const message = generated.message;
    const type = getContentType(message);
    const holder = type && message[type];
    if (holder && typeof holder === 'object') {
        holder.contextInfo = { ...(holder.contextInfo || {}), ...patch };
    }
    return generated;
};

const injectMentions = (content, jids = []) => {
    if (!content || typeof content !== 'object') {
        return content;
    }
    const mentions = [...new Set(jids.map(jid => jidNormalizedUser(jid)).filter(Boolean))];
    if (!mentions.length) {
        return content;
    }
    const result = { ...content };
    if (typeof result.text === 'string') {
        const text = result.text;
        delete result.text;
        result.extendedTextMessage = {
            text: `${text}${mentions.map(jid => ` @${jid.split('@')[0]}`).join('')}`.trim(),
            contextInfo: { mentionedJid: mentions }
        };
        return result;
    }
    const holderKey = CONTEXT_HOLDER_KEYS.find(key => result[key] !== undefined && result[key] !== null);
    if (!holderKey) {
        return { ...result, mentions };
    }
    if (holderKey === 'conversation') {
        const text = String(result.conversation);
        delete result.conversation;
        result.extendedTextMessage = {
            text: `${text}${mentions.map(jid => ` @${jid.split('@')[0]}`).join('')}`.trim(),
            contextInfo: { mentionedJid: mentions }
        };
        return result;
    }
    result[holderKey] = {
        ...result[holderKey],
        contextInfo: {
            ...(result[holderKey].contextInfo || {}),
            mentionedJid: [...new Set([...(result[holderKey].contextInfo?.mentionedJid || []), ...mentions])]
        }
    };
    return result;
};

export const buildFakeQuote = (fake = {}, jid = '') => ({
    quoted: {
        key: {
            remoteJid: jid,
            participant: fake.participant || jid,
            id: fake.stanzaId || 'fake-quote',
            fromMe: false
        },
        message: fake.quotedMessage || proto.Message.fromObject({
            conversation: fake.text || ''
        })
    }
});

export const createStatusMentionHelper = ({
    sendMessage,
    relayMessage,
    getGroupMetadata,
    getOwnJid,
    getPn,
    resolveJid,
    logger
}) => async (first, second, third) => {
    const isArrayFirst = Array.isArray(first);
    const content = isArrayFirst ? second : first;
    const jids = isArrayFirst ? first : second;
    const options = third || {};
    const targets = [];
    const groupTargets = new Set();
    const toPnJid = jid => {
        const normalized = jidNormalizedUser(jid);
        return typeof getPn === 'function' ? getPn(normalized) : normalized;
    };
    for (const jid of jids || []) {
        if (!jid || jid === 'status@broadcast') {
            continue;
        }
        if (isJidGroup(jid)) {
            const metadata = await getGroupMetadata(jid);
            for (const participant of metadata?.participants || []) {
                if (!participant.jid) {
                    continue;
                }
                const pnJid = toPnJid(participant.jid);
                targets.push(pnJid);
                groupTargets.add(pnJid);
            }
        }
        else {
            targets.push(toPnJid(jid));
        }
    }
    const ownJid = jidNormalizedUser(getOwnJid() || '');
    if (ownJid) {
        targets.push(ownJid);
    }
    const statusJidList = [...new Set(targets
        .map(jid => toPnJid(jid))
        .filter(jid => jid && /@s\.whatsapp\.net$/.test(jid)))];
    const status = await sendMessage('status@broadcast', content, {
        ...options,
        statusJidList
    });
    for (const target of statusJidList) {
        if (target === ownJid) {
            continue;
        }
        const type = groupTargets.has(target) ? 'groupStatusMentionMessage' : 'statusMentionMessage';
        try {
            await relayMessage(target, {
                [type]: {
                    message: proto.Message.fromObject({
                        conversation: options.mentionText || 'Mention status'
                    })
                }
            }, { messageId: generateMessageIDV2(getOwnJid()) });
        }
        catch (err) {
            logger?.debug?.({ err: err?.message, target }, 'failed to send status mention notification');
        }
    }
    return status;
};

export const createHidetagHelper = ({ sendMessage, getGroupMetadata, resolveJid }) => async (
    groupJid,
    content,
    options = {}
) => {
    const metadata = await getGroupMetadata(groupJid);
    const jids = (metadata?.participants || []).map(participant => participant.jid).filter(Boolean);
    return sendMessage(resolveJid(groupJid), injectMentions(content, jids), options);
};

export const createReadViewOnceHelper = ({ downloadMedia }) => async (msg) => {
    const candidates = [
        msg?.message ?? msg,
        msg?.quotedMessage?.message ?? msg?.quotedMessage
    ];
    for (const candidate of candidates) {
        const content = unwrapMessageContent(candidate);
        const type = MEDIA_MESSAGE_KEYS.find(key => content?.[key]);
        if (!type) {
            continue;
        }
        const buffer = await downloadMedia(msg);
        const media = content[type];
        return {
            buffer,
            mimetype: media.mimetype || '',
            type: type.replace(/Message$/, ''),
            caption: media.caption || ''
        };
    }
    throw new Boom('Message does not contain view-once media', { statusCode: 400, data: msg });
};

export const createReplyHelper = (sendMessage) => (jid, content, quoted, options = {}) => sendMessage(
    jid,
    typeof content === 'string' ? { text: content } : content,
    { quoted, ...options }
);

export const createDownloadMediaHelper = ({ options, ctx, download = downloadMediaMessage } = {}) => async (message, type = 'buffer') => {
    const candidates = [
        message?.message ?? message,
        message?.quotedMessage?.message ?? message?.quotedMessage
    ];
    for (const candidate of candidates) {
        const content = unwrapMessageContent(candidate);
        if (MEDIA_MESSAGE_KEYS.some(key => content?.[key])) {
            const wrapped = {
                key: message?.key ?? message?.quotedMessage?.key,
                message: content
            };
            if (type === 'stream') {
                return download(wrapped, 'stream', options, ctx);
            }
            const buffer = await download(wrapped, 'buffer', options, ctx);
            if (type === 'base64') {
                return buffer.toString('base64');
            }
            if (type === 'data-url') {
                const holderKey = MEDIA_MESSAGE_KEYS.find(key => content?.[key]);
                const mimetype = content[holderKey]?.mimetype || 'application/octet-stream';
                return `data:${mimetype};base64,${buffer.toString('base64')}`;
            }
            return buffer;
        }
    }
    throw new Boom('Message does not contain media', { statusCode: 400, data: message });
};

export const makeMessagesSocket = (config) => {
    const { logger, linkPreviewImageThumbnailWidth, generateHighQualityLinkPreview, options: httpRequestOptions, patchMessageBeforeSending, cachedGroupMetadata, enableRecentMessageCache, maxMsgRetryCount } = config;
    const sock = makeNewsletterSocket(config);
    const { ev, authState, messageMutex, signalRepository, upsertMessage, query, fetchPrivacySettings, sendNode, groupMetadata, groupToggleEphemeral, registerSocketEndHandler } = sock;
    const getLIDForPN = signalRepository.lidMapping.getLIDForPN.bind(signalRepository.lidMapping);
    /**
     * Set of tctoken storage JIDs with a fire-and-forget `issuePrivacyTokens` IQ in flight.
     * Prevents duplicate IQs from rapid back-to-back sends before `senderTimestamp` persists.
     * Entries are always removed in `.finally()`, so the set is bounded by concurrency.
     */
    const inFlightTcTokenIssuance = new Set();
    const userDevicesCache = config.userDevicesCache ||
        new NodeCache({
            stdTTL: DEFAULT_CACHE_TTLS.USER_DEVICES, // 5 minutes
            useClones: false
        });
    /** Serializes writes to userDevicesCache across USync refresh and device-notification handling. */
    const devicesMutex = makeMutex();
    // Initialize message retry manager if enabled
    const messageRetryManager = enableRecentMessageCache ? new MessageRetryManager(logger, maxMsgRetryCount) : null;
    // Prevent race conditions in Signal session encryption by user
    const encryptionMutex = makeKeyedMutex();
    let mediaConn;
    /** Per-socket media host; updated whenever media_conn is fetched. Defaults to the public WhatsApp host. */
    let mediaHost = DEF_MEDIA_HOST;
    const refreshMediaConn = async (forceGet = false) => {
        const media = await mediaConn;
        if (!media || forceGet || new Date().getTime() - media.fetchDate.getTime() > media.ttl * 1000) {
            mediaConn = (async () => {
                const result = await query({
                    tag: 'iq',
                    attrs: {
                        type: 'set',
                        xmlns: 'w:m',
                        to: S_WHATSAPP_NET
                    },
                    content: [{ tag: 'media_conn', attrs: {} }]
                });
                const mediaConnNode = getBinaryNodeChild(result, 'media_conn');
                // TODO: explore full length of data that whatsapp provides
                const node = {
                    hosts: getBinaryNodeChildren(mediaConnNode, 'host').map(({ attrs }) => ({
                        hostname: attrs.hostname,
                        maxContentLengthBytes: +attrs.maxContentLengthBytes
                    })),
                    auth: mediaConnNode.attrs.auth,
                    ttl: +mediaConnNode.attrs.ttl,
                    fetchDate: new Date()
                };
                logger.debug('fetched media conn');
                if (node.hosts[0]) {
                    mediaHost = node.hosts[0].hostname;
                }
                return node;
            })();
        }
        return mediaConn;
    };
    /**
     * generic send receipt function
     * used for receipts of phone call, read, delivery etc.
     * */
    const sendReceipt = async (jid, participant, messageIds, type) => {
        if (!messageIds || messageIds.length === 0) {
            throw new Boom('missing ids in receipt');
        }
        const node = {
            tag: 'receipt',
            attrs: {
                id: messageIds[0]
            }
        };
        const isReadReceipt = type === 'read' || type === 'read-self';
        if (isReadReceipt) {
            node.attrs.t = unixTimestampSeconds().toString();
        }
        if (type === 'sender' && (isPnUser(jid) || isLidUser(jid))) {
            node.attrs.recipient = jid;
            node.attrs.to = participant;
        }
        else {
            node.attrs.to = jid;
            if (participant) {
                node.attrs.participant = participant;
            }
        }
        if (type) {
            node.attrs.type = type;
        }
        const remainingMessageIds = messageIds.slice(1);
        if (remainingMessageIds.length) {
            node.content = [
                {
                    tag: 'list',
                    attrs: {},
                    content: remainingMessageIds.map(id => ({
                        tag: 'item',
                        attrs: { id }
                    }))
                }
            ];
        }
        logger.debug({ attrs: node.attrs, messageIds }, 'sending receipt for messages');
        await sendNode(node);
    };
    /** Correctly bulk send receipts to multiple chats, participants */
    const sendReceipts = async (keys, type) => {
        const recps = aggregateMessageKeysNotFromMe(keys);
        for (const { jid, participant, messageIds } of recps) {
            await sendReceipt(jid, participant, messageIds, type);
        }
    };
    /** Bulk read messages. Keys can be from different chats & participants */
    const readMessages = async (keys) => {
        const privacySettings = await fetchPrivacySettings();
        // based on privacy settings, we have to change the read type
        const readType = privacySettings.readreceipts === 'all' ? 'read' : 'read-self';
        await sendReceipts(keys, readType);
    };
    /** Fetch all the devices we've to send a message to */
    const getUSyncDevices = async (jids, useCache, ignoreZeroDevices) => {
        const deviceResults = [];
        if (!useCache) {
            logger.debug('not using cache for devices');
        }
        const toFetch = [];
        const jidsWithUser = jids
            .map(jid => {
            const decoded = jidDecode(jid);
            const user = decoded?.user;
            const device = decoded?.device;
            const isExplicitDevice = typeof device === 'number' && device >= 0;
            if (isExplicitDevice && user) {
                deviceResults.push({
                    user,
                    device,
                    jid
                });
                return null;
            }
            jid = jidNormalizedUser(jid);
            return { jid, user };
        })
            .filter(jid => jid !== null);
        let mgetDevices;
        if (useCache && userDevicesCache.mget) {
            const usersToFetch = jidsWithUser.map(j => j?.user).filter(Boolean);
            mgetDevices = await userDevicesCache.mget(usersToFetch);
        }
        for (const { jid, user } of jidsWithUser) {
            if (useCache) {
                const devices = mgetDevices?.[user] ||
                    (userDevicesCache.mget ? undefined : (await userDevicesCache.get(user)));
                if (devices) {
                    const devicesWithJid = devices.map(d => ({
                        ...d,
                        jid: jidEncode(d.user, d.server, d.device)
                    }));
                    deviceResults.push(...devicesWithJid);
                    logger.trace({ user }, 'using cache for devices');
                }
                else {
                    toFetch.push(jid);
                }
            }
            else {
                toFetch.push(jid);
            }
        }
        if (!toFetch.length) {
            return deviceResults;
        }
        const requestedLidUsers = new Set();
        for (const jid of toFetch) {
            if (isLidUser(jid) || isHostedLidUser(jid)) {
                const user = jidDecode(jid)?.user;
                if (user)
                    requestedLidUsers.add(user);
            }
        }
        const query = new USyncQuery().withContext('message').withDeviceProtocol().withLIDProtocol();
        for (const jid of toFetch) {
            query.withUser(new USyncUser().withId(jid)); // todo: investigate - the idea here is that <user> should have an inline lid field with the lid being the pn equivalent
        }
        const result = await sock.executeUSyncQuery(query);
        if (result) {
            // TODO: LID MAP this stuff (lid protocol will now return lid with devices)
            const lidResults = result.list.filter(a => !!a.lid);
            if (lidResults.length > 0) {
                logger.trace('Storing LID maps from device call');
                await signalRepository.lidMapping.storeLIDPNMappings(lidResults.map(a => ({ lid: a.lid, pn: a.id })));
                // Force-refresh sessions for newly mapped LIDs to align identity addressing
                try {
                    const lids = lidResults.map(a => a.lid);
                    if (lids.length) {
                        await assertSessions(lids, true);
                    }
                }
                catch (e) {
                    logger.warn({ e, count: lidResults.length }, 'failed to assert sessions for newly mapped LIDs');
                }
            }
            const extracted = extractDeviceJids(result?.list, authState.creds.me.id, authState.creds.me.lid, ignoreZeroDevices);
            const deviceMap = {};
            for (const item of extracted) {
                deviceMap[item.user] = deviceMap[item.user] || [];
                deviceMap[item.user]?.push(item);
            }
            // Process each user's devices as a group for bulk LID migration
            for (const [user, userDevices] of Object.entries(deviceMap)) {
                const isLidUser = requestedLidUsers.has(user);
                // Process all devices for this user
                for (const item of userDevices) {
                    const finalJid = isLidUser
                        ? jidEncode(user, item.server, item.device)
                        : jidEncode(item.user, item.server, item.device);
                    deviceResults.push({
                        ...item,
                        jid: finalJid
                    });
                    logger.debug({
                        user: item.user,
                        device: item.device,
                        finalJid,
                        usedLid: isLidUser
                    }, 'Processed device with LID priority');
                }
            }
            await devicesMutex.mutex(async () => {
                if (userDevicesCache.mset) {
                    // if the cache supports mset, we can set all devices in one go
                    await userDevicesCache.mset(Object.entries(deviceMap).map(([key, value]) => ({ key, value })));
                }
                else {
                    for (const key in deviceMap) {
                        if (deviceMap[key])
                            await userDevicesCache.set(key, deviceMap[key]);
                    }
                }
            });
            const userDeviceUpdates = {};
            for (const [userId, devices] of Object.entries(deviceMap)) {
                if (devices && devices.length > 0) {
                    userDeviceUpdates[userId] = devices.map(d => d.device?.toString() || '0');
                }
            }
            if (Object.keys(userDeviceUpdates).length > 0) {
                try {
                    await authState.keys.set({ 'device-list': userDeviceUpdates });
                    logger.debug({ userCount: Object.keys(userDeviceUpdates).length }, 'stored user device lists for bulk migration');
                }
                catch (error) {
                    logger.warn({ error }, 'failed to store user device lists');
                }
            }
        }
        return deviceResults;
    };
    /**
     * Update Member Label
     */
    const updateMemberLabel = (jid, memberLabel) => {
        return relayMessage(jid, {
            protocolMessage: {
                type: proto.Message.ProtocolMessage.Type.GROUP_MEMBER_LABEL_CHANGE,
                memberLabel: {
                    label: memberLabel?.slice(0, 30),
                    labelTimestamp: unixTimestampSeconds()
                }
            }
        }, {
            additionalNodes: [
                {
                    tag: 'meta',
                    attrs: {
                        tag_reason: 'user_update',
                        appdata: 'member_tag'
                    },
                    content: undefined
                }
            ]
        });
    };
    const assertSessions = async (jids, force) => {
        let didFetchNewSession = false;
        const uniqueJids = [...new Set(jids)];
        const jidsRequiringFetch = [];
        logger.debug({ jids }, 'assertSessions call with jids');
        for (const jid of uniqueJids) {
            if (!force) {
                const sessionValidation = await signalRepository.validateSession(jid);
                if (sessionValidation.exists) {
                    continue;
                }
            }
            jidsRequiringFetch.push(jid);
        }
        if (jidsRequiringFetch.length) {
            // LID if mapped, otherwise original
            const wireJids = [
                ...jidsRequiringFetch.filter(jid => !!isLidUser(jid) || !!isHostedLidUser(jid)),
                ...((await signalRepository.lidMapping.getLIDsForPNs(jidsRequiringFetch.filter(jid => !!isPnUser(jid) || !!isHostedPnUser(jid)))) || []).map(a => a.lid)
            ];
            logger.debug({ jidsRequiringFetch, wireJids }, 'fetching sessions');
            const result = await query({
                tag: 'iq',
                attrs: {
                    xmlns: 'encrypt',
                    type: 'get',
                    to: S_WHATSAPP_NET
                },
                content: [
                    {
                        tag: 'key',
                        attrs: {},
                        content: wireJids.map(jid => {
                            const attrs = { jid };
                            if (force)
                                attrs.reason = 'identity';
                            return { tag: 'user', attrs };
                        })
                    }
                ]
            });
            await parseAndInjectE2ESessions(result, signalRepository);
            didFetchNewSession = true;
        }
        return didFetchNewSession;
    };
    const sendPeerDataOperationMessage = async (pdoMessage) => {
        //TODO: for later, abstract the logic to send a Peer Message instead of just PDO - useful for App State Key Resync with phone
        if (!authState.creds.me?.id) {
            throw new Boom('Not authenticated');
        }
        const protocolMessage = {
            protocolMessage: {
                peerDataOperationRequestMessage: pdoMessage,
                type: proto.Message.ProtocolMessage.Type.PEER_DATA_OPERATION_REQUEST_MESSAGE
            }
        };
        const meJid = jidNormalizedUser(authState.creds.me.id);
        const msgId = await relayMessage(meJid, protocolMessage, {
            additionalAttributes: {
                category: 'peer',
                push_priority: 'high_force'
            },
            additionalNodes: [
                {
                    tag: 'meta',
                    attrs: { appdata: 'default' }
                }
            ]
        });
        return msgId;
    };
    const createParticipantNodes = async (recipientJids, message, extraAttrs, dsmMessage) => {
        if (!recipientJids.length) {
            return { nodes: [], shouldIncludeDeviceIdentity: false };
        }
        const patched = await patchMessageBeforeSending(message, recipientJids);
        const patchedMessages = Array.isArray(patched)
            ? patched
            : recipientJids.map(jid => ({ recipientJid: jid, message: patched }));
        let shouldIncludeDeviceIdentity = false;
        const meId = authState.creds.me.id;
        const meLid = authState.creds.me?.lid;
        const meLidUser = meLid ? jidDecode(meLid)?.user : null;
        const encryptionPromises = patchedMessages.map(async ({ recipientJid: jid, message: patchedMessage }) => {
            try {
                if (!jid)
                    return null;
                let msgToEncrypt = patchedMessage;
                if (dsmMessage) {
                    const { user: targetUser } = jidDecode(jid);
                    const { user: ownPnUser } = jidDecode(meId);
                    const ownLidUser = meLidUser;
                    const isOwnUser = targetUser === ownPnUser || (ownLidUser && targetUser === ownLidUser);
                    const isExactSenderDevice = jid === meId || (meLid && jid === meLid);
                    if (isOwnUser && !isExactSenderDevice) {
                        msgToEncrypt = dsmMessage;
                        logger.debug({ jid, targetUser }, 'Using DSM for own device');
                    }
                }
                const bytes = encodeWAMessage(msgToEncrypt);
                const mutexKey = jid;
                const node = await encryptionMutex.mutex(mutexKey, async () => {
                    const { type, ciphertext } = await signalRepository.encryptMessage({ jid, data: bytes });
                    if (type === 'pkmsg') {
                        shouldIncludeDeviceIdentity = true;
                    }
                    return {
                        tag: 'to',
                        attrs: { jid },
                        content: [
                            {
                                tag: 'enc',
                                attrs: { v: '2', type, ...(extraAttrs || {}) },
                                content: ciphertext
                            }
                        ]
                    };
                });
                return node;
            }
            catch (err) {
                logger.error({ jid, err }, 'Failed to encrypt for recipient');
                return null;
            }
        });
        const nodes = (await Promise.all(encryptionPromises)).filter(node => node !== null);
        if (recipientJids.length > 0 && nodes.length === 0) {
            throw new Boom('All encryptions failed', { statusCode: 500 });
        }
        return { nodes, shouldIncludeDeviceIdentity };
    };
    const getInteractiveNode = buildInteractiveBizNode;
    const rawRelayMessage = async (jid, message, { messageId: msgId, participant, additionalAttributes, additionalNodes, useUserDevicesCache, useCachedGroupMetadata, statusJidList }) => {
        jid = sanitizeJid(jid);
        const meId = assertMeId(authState.creds);
        const meLid = authState.creds.me?.lid;
        const isRetryResend = Boolean(participant?.jid);
        let shouldIncludeDeviceIdentity = isRetryResend;
        const statusJid = 'status@broadcast';
        const { user, server } = jidDecode(jid);
        const isGroup = server === 'g.us';
        const isStatus = jid === statusJid;
        const isLid = server === 'lid';
        const isNewsletter = server === 'newsletter';
        const isGroupOrStatus = isGroup || isStatus;
        const finalJid = jid;
        msgId = msgId || generateMessageIDV2(meId);
        additionalNodes = mergeAdditionalNodes(additionalNodes, getInteractiveNode(message));
        useUserDevicesCache = useUserDevicesCache !== false;
        useCachedGroupMetadata = useCachedGroupMetadata !== false && !isStatus;
        const participants = [];
        const destinationJid = !isStatus ? finalJid : statusJid;
        const binaryNodeContent = [];
        const devices = [];
        let reportingMessage;
        const meMsg = {
            deviceSentMessage: {
                destinationJid,
                message
            },
            messageContextInfo: message.messageContextInfo
        };
        const extraAttrs = {};
        if (participant) {
            if (!isGroup && !isStatus) {
                additionalAttributes = { ...additionalAttributes, device_fanout: 'false' };
            }
            const { user, device } = jidDecode(participant.jid);
            devices.push({
                user,
                device,
                jid: participant.jid
            });
        }
        await authState.keys.transaction(async () => {
            const mediaType = getMediaType(message);
            if (mediaType) {
                extraAttrs['mediatype'] = mediaType;
            }
            if (isNewsletter) {
                const patched = patchMessageBeforeSending ? await patchMessageBeforeSending(message, []) : message;
                const bytes = encodeNewsletterMessage(patched);
                binaryNodeContent.push({
                    tag: 'plaintext',
                    attrs: {},
                    content: bytes
                });
                const stanza = {
                    tag: 'message',
                    attrs: {
                        to: jid,
                        id: msgId,
                        type: getMessageType(message),
                        ...(additionalAttributes || {})
                    },
                    content: binaryNodeContent
                };
                logger.debug({ msgId }, `sending newsletter message to ${jid}`);
                await sendNode(stanza);
                return;
            }
            if (normalizeMessageContent(message)?.pinInChatMessage || normalizeMessageContent(message)?.reactionMessage) {
                extraAttrs['decrypt-fail'] = 'hide'; // todo: expand for reactions and other types
            }
            if (isGroupOrStatus && !isRetryResend) {
                const [groupData, senderKeyMap] = await Promise.all([
                    (async () => {
                        let groupData = useCachedGroupMetadata && cachedGroupMetadata ? await cachedGroupMetadata(jid) : undefined; // todo: should we rely on the cache specially if the cache is outdated and the metadata has new fields?
                        if (groupData && Array.isArray(groupData?.participants)) {
                            logger.trace({ jid, participants: groupData.participants.length }, 'using cached group metadata');
                        }
                        else if (!isStatus) {
                            groupData = await groupMetadata(jid); // TODO: start storing group participant list + addr mode in Signal & stop relying on this
                        }
                        return groupData;
                    })(),
                    (async () => {
                        if (!participant && !isStatus) {
                            // what if sender memory is less accurate than the cached metadata
                            // on participant change in group, we should do sender memory manipulation
                            const result = await authState.keys.get('sender-key-memory', [jid]); // TODO: check out what if the sender key memory doesn't include the LID stuff now?
                            return result[jid] || {};
                        }
                        return {};
                    })()
                ]);
                const participantsList = groupData ? groupData.participants.map(p => p.id) : [];
                if (groupData?.ephemeralDuration && groupData.ephemeralDuration > 0) {
                    additionalAttributes = {
                        ...additionalAttributes,
                        expiration: groupData.ephemeralDuration.toString()
                    };
                }
                if (isStatus && statusJidList) {
                    participantsList.push(...statusJidList);
                }
                const additionalDevices = await getUSyncDevices(participantsList, !!useUserDevicesCache, false);
                devices.push(...additionalDevices);
                if (isGroup) {
                    additionalAttributes = {
                        ...additionalAttributes,
                        addressing_mode: groupData?.addressingMode || 'lid'
                    };
                }
                const patched = await patchMessageBeforeSending(message);
                if (Array.isArray(patched)) {
                    throw new Boom('Per-jid patching is not supported in groups');
                }
                const bytes = encodeWAMessage(patched);
                reportingMessage = patched;
                const groupAddressingMode = additionalAttributes?.['addressing_mode'] || groupData?.addressingMode || 'lid';
                const groupSenderIdentity = groupAddressingMode === 'lid' && meLid ? meLid : meId;
                const { ciphertext, senderKeyDistributionMessage } = await signalRepository.encryptGroupMessage({
                    group: destinationJid,
                    data: bytes,
                    meId: groupSenderIdentity
                });
                const senderKeyRecipients = [];
                for (const device of devices) {
                    const deviceJid = device.jid;
                    const hasKey = !!senderKeyMap[deviceJid];
                    if ((!hasKey || !!participant) &&
                        !isHostedLidUser(deviceJid) &&
                        !isHostedPnUser(deviceJid) &&
                        device.device !== 99) {
                        //todo: revamp all this logic
                        // the goal is to follow with what I said above for each group, and instead of a true false map of ids, we can set an array full of those the app has already sent pkmsgs
                        senderKeyRecipients.push(deviceJid);
                        senderKeyMap[deviceJid] = true;
                    }
                }
                if (senderKeyRecipients.length) {
                    logger.debug({ senderKeyJids: senderKeyRecipients }, 'sending new sender key');
                    const senderKeyMsg = {
                        senderKeyDistributionMessage: {
                            axolotlSenderKeyDistributionMessage: senderKeyDistributionMessage,
                            groupId: destinationJid
                        }
                    };
                    const senderKeySessionTargets = senderKeyRecipients;
                    await assertSessions(senderKeySessionTargets);
                    const result = await createParticipantNodes(senderKeyRecipients, senderKeyMsg, extraAttrs);
                    shouldIncludeDeviceIdentity = shouldIncludeDeviceIdentity || result.shouldIncludeDeviceIdentity;
                    participants.push(...result.nodes);
                }
                binaryNodeContent.push({
                    tag: 'enc',
                    attrs: { v: '2', type: 'skmsg', ...extraAttrs },
                    content: ciphertext
                });
                await authState.keys.set({ 'sender-key-memory': { [jid]: senderKeyMap } });
            }
            else {
                // ADDRESSING CONSISTENCY: Match own identity to conversation context
                // TODO: investigate if this is true
                let ownId = meId;
                if (isLid && meLid) {
                    ownId = meLid;
                    logger.debug({ to: jid, ownId }, 'Using LID identity for @lid conversation');
                }
                else {
                    logger.debug({ to: jid, ownId }, 'Using PN identity for @s.whatsapp.net conversation');
                }
                const { user: ownUser } = jidDecode(ownId);
                if (!participant) {
                    const patchedForReporting = await patchMessageBeforeSending(message, [jid]);
                    reportingMessage = Array.isArray(patchedForReporting)
                        ? patchedForReporting.find(item => item.recipientJid === jid) || patchedForReporting[0]
                        : patchedForReporting;
                }
                if (!isRetryResend) {
                    const targetUserServer = isLid ? 'lid' : 's.whatsapp.net';
                    devices.push({
                        user,
                        device: 0,
                        jid: jidEncode(user, targetUserServer, 0) // rajeh, todo: this entire logic is convoluted and weird.
                    });
                    if (user !== ownUser) {
                        const ownUserServer = isLid ? 'lid' : 's.whatsapp.net';
                        const ownUserForAddressing = isLid && meLid ? jidDecode(meLid).user : jidDecode(meId).user;
                        devices.push({
                            user: ownUserForAddressing,
                            device: 0,
                            jid: jidEncode(ownUserForAddressing, ownUserServer, 0)
                        });
                    }
                    if (additionalAttributes?.['category'] !== 'peer') {
                        // Clear placeholders and enumerate actual devices
                        devices.length = 0;
                        // Use conversation-appropriate sender identity
                        const senderIdentity = isLid && meLid
                            ? jidEncode(jidDecode(meLid)?.user, 'lid', undefined)
                            : jidEncode(jidDecode(meId)?.user, 's.whatsapp.net', undefined);
                        // Enumerate devices for sender and target with consistent addressing
                        const sessionDevices = await getUSyncDevices([senderIdentity, jid], true, false);
                        devices.push(...sessionDevices);
                        logger.debug({
                            deviceCount: devices.length,
                            devices: devices.map(d => `${d.user}:${d.device}@${jidDecode(d.jid)?.server}`)
                        }, 'Device enumeration complete with unified addressing');
                    }
                }
                const allRecipients = [];
                const meRecipients = [];
                const otherRecipients = [];
                const { user: mePnUser } = jidDecode(meId);
                const { user: meLidUser } = meLid ? jidDecode(meLid) : { user: null };
                for (const { user, jid } of devices) {
                    const isExactSenderDevice = jid === meId || (meLid && jid === meLid);
                    if (isExactSenderDevice) {
                        logger.debug({ jid, meId, meLid }, 'Skipping exact sender device (whatsmeow pattern)');
                        continue;
                    }
                    // Check if this is our device (could match either PN or LID user)
                    const isMe = user === mePnUser || user === meLidUser;
                    if (isMe) {
                        meRecipients.push(jid);
                    }
                    else {
                        otherRecipients.push(jid);
                    }
                    allRecipients.push(jid);
                }
                await assertSessions(allRecipients);
                const [{ nodes: meNodes, shouldIncludeDeviceIdentity: s1 }, { nodes: otherNodes, shouldIncludeDeviceIdentity: s2 }] = await Promise.all([
                    // For own devices: use DSM if available (1:1 chats only)
                    createParticipantNodes(meRecipients, meMsg || message, extraAttrs),
                    createParticipantNodes(otherRecipients, message, extraAttrs, meMsg)
                ]);
                participants.push(...meNodes);
                participants.push(...otherNodes);
                if (meRecipients.length > 0 || otherRecipients.length > 0) {
                    extraAttrs['phash'] = generateParticipantHashV2([...meRecipients, ...otherRecipients]);
                }
                shouldIncludeDeviceIdentity = shouldIncludeDeviceIdentity || s1 || s2;
            }
            if (isRetryResend) {
                const isParticipantLid = isLidUser(participant.jid);
                const isMe = areJidsSameUser(participant.jid, isParticipantLid ? meLid : meId);
                let messageToSend = message;
                if (isGroupOrStatus) {
                    let groupSenderIdentity;
                    if (meLid && (await signalRepository.hasSenderKey({ group: destinationJid, meId: meLid }))) {
                        groupSenderIdentity = meLid;
                    }
                    else if (await signalRepository.hasSenderKey({ group: destinationJid, meId })) {
                        groupSenderIdentity = meId;
                    }
                    if (groupSenderIdentity) {
                        try {
                            const skdm = await signalRepository.getSenderKeyDistributionMessage({
                                group: destinationJid,
                                meId: groupSenderIdentity
                            });
                            messageToSend = {
                                ...message,
                                senderKeyDistributionMessage: {
                                    groupId: destinationJid,
                                    axolotlSenderKeyDistributionMessage: skdm
                                }
                            };
                        }
                        catch (err) {
                            logger.warn({ err, jid: destinationJid }, 'failed to build SKDM for retry, sending without it');
                        }
                    }
                }
                const encodedMessageToSend = isMe
                    ? encodeWAMessage({
                        deviceSentMessage: {
                            destinationJid,
                            message: messageToSend
                        }
                    })
                    : encodeWAMessage(messageToSend);
                const { type, ciphertext: encryptedContent } = await signalRepository.encryptMessage({
                    data: encodedMessageToSend,
                    jid: participant.jid
                });
                binaryNodeContent.push({
                    tag: 'enc',
                    attrs: {
                        v: '2',
                        type,
                        count: participant.count.toString()
                    },
                    content: encryptedContent
                });
            }
            if (participants.length) {
                if (additionalAttributes?.['category'] === 'peer') {
                    const peerNode = participants[0]?.content?.[0];
                    if (peerNode) {
                        binaryNodeContent.push(peerNode); // push only enc
                    }
                }
                else {
                    binaryNodeContent.push({
                        tag: 'participants',
                        attrs: {},
                        content: participants
                    });
                }
            }
            const stanza = {
                tag: 'message',
                attrs: {
                    id: msgId,
                    to: destinationJid,
                    type: getMessageType(message),
                    ...(additionalAttributes || {})
                },
                content: binaryNodeContent
            };
            // if the participant to send to is explicitly specified (generally retry recp)
            // ensure the message is only sent to that person
            // if a retry receipt is sent to everyone -- it'll fail decryption for everyone else who received the msg
            if (participant) {
                if (isJidGroup(destinationJid)) {
                    stanza.attrs.to = destinationJid;
                    stanza.attrs.participant = participant.jid;
                }
                else if (areJidsSameUser(participant.jid, meId)) {
                    stanza.attrs.to = participant.jid;
                    stanza.attrs.recipient = destinationJid;
                }
                else {
                    stanza.attrs.to = participant.jid;
                }
            }
            else {
                stanza.attrs.to = destinationJid;
            }
            if (shouldIncludeDeviceIdentity) {
                ;
                stanza.content.push({
                    tag: 'device-identity',
                    attrs: {},
                    content: encodeSignedDeviceIdentity(authState.creds.account, true)
                });
                logger.debug({ jid }, 'adding device identity');
            }
            if (!isNewsletter &&
                !isRetryResend &&
                reportingMessage?.messageContextInfo?.messageSecret &&
                shouldIncludeReportingToken(reportingMessage)) {
                try {
                    const encoded = encodeWAMessage(reportingMessage);
                    const reportingKey = {
                        id: msgId,
                        fromMe: true,
                        remoteJid: destinationJid,
                        participant: participant?.jid
                    };
                    const reportingNode = await getMessageReportingToken(encoded, reportingMessage, reportingKey);
                    if (reportingNode) {
                        ;
                        stanza.content.push(reportingNode);
                        logger.trace({ jid }, 'added reporting token to message');
                    }
                }
                catch (error) {
                    logger.warn({ jid, trace: error?.stack }, 'failed to attach reporting token');
                }
            }
            // WA Web never attaches tctoken to peer (AppStateSync) messages — server rejects with 479
            const isPeerMessage = additionalAttributes?.['category'] === 'peer';
            const is1on1Send = !isGroup && !isRetryResend && !isStatus && !isNewsletter && !isPeerMessage;
            // Resolve destination to LID for tctoken storage — matches Signal session key pattern
            const tcTokenJid = is1on1Send ? await resolveTcTokenJid(destinationJid, getLIDForPN) : destinationJid;
            const contactTcTokenData = is1on1Send ? await authState.keys.get('tctoken', [tcTokenJid]) : {};
            const existingTokenEntry = contactTcTokenData[tcTokenJid];
            let tcTokenBuffer = existingTokenEntry?.token;
            // Treat expired tokens the same as missing — clear from cache
            if (tcTokenBuffer?.length && isTcTokenExpired(existingTokenEntry?.timestamp)) {
                logger.debug({ jid: destinationJid, timestamp: existingTokenEntry?.timestamp }, 'tctoken expired, clearing');
                tcTokenBuffer = undefined;
                // Preserve senderTimestamp so the fire-and-forget issuance dedupe survives cleanup.
                const cleared = existingTokenEntry?.senderTimestamp !== undefined
                    ? { token: Buffer.alloc(0), senderTimestamp: existingTokenEntry.senderTimestamp }
                    : null;
                try {
                    await authState.keys.set({ tctoken: { [tcTokenJid]: cleared } });
                }
                catch (err) {
                    logger.debug({ jid: destinationJid, err: err?.message }, 'failed to persist tctoken expiry cleanup');
                }
            }
            if (tcTokenBuffer?.length && sock.serverProps.privacyTokenOn1to1) {
                ;
                stanza.content.push({
                    tag: 'tctoken',
                    attrs: {},
                    content: tcTokenBuffer
                });
            }
            if (additionalNodes && additionalNodes.length > 0) {
                ;
                stanza.content.push(...additionalNodes);
            }
            logger.debug({ msgId }, `sending message to ${participants.length} devices`);
            await sendNode(stanza);
            // Fire-and-forget: issue our token to the contact AFTER message send.
            // WA Web skips protocol messages and PSA/bot contacts (TcTokenChatAction: isRegularUser)
            const isProtocolMsg = !!normalizeMessageContent(message)?.protocolMessage;
            const isBotOrPSA = destinationJid === PSA_WID || isJidBot(destinationJid) || isJidMetaAI(destinationJid);
            if (is1on1Send &&
                !isProtocolMsg &&
                !isBotOrPSA &&
                shouldSendNewTcToken(existingTokenEntry?.senderTimestamp) &&
                !inFlightTcTokenIssuance.has(tcTokenJid)) {
                inFlightTcTokenIssuance.add(tcTokenJid);
                const issueTimestamp = unixTimestampSeconds();
                const getPNForLID = signalRepository.lidMapping.getPNForLID.bind(signalRepository.lidMapping);
                resolveIssuanceJid(destinationJid, sock.serverProps.lidTrustedTokenIssueToLid, getLIDForPN, getPNForLID)
                    .then(issueJid => issuePrivacyTokens([issueJid], issueTimestamp))
                    .then(async (result) => {
                    await storeTcTokensFromIqResult({
                        result,
                        fallbackJid: tcTokenJid,
                        keys: authState.keys,
                        getLIDForPN
                    });
                    const currentData = await authState.keys.get('tctoken', [tcTokenJid]);
                    const currentEntry = currentData[tcTokenJid];
                    const indexWrite = await buildMergedTcTokenIndexWrite(authState.keys, [tcTokenJid]);
                    await authState.keys.set({
                        tctoken: {
                            [tcTokenJid]: {
                                token: Buffer.alloc(0),
                                ...currentEntry,
                                senderTimestamp: issueTimestamp
                            },
                            ...indexWrite
                        }
                    });
                })
                    .catch(err => {
                    logger.debug({ jid: destinationJid, err: err?.message }, 'fire-and-forget tctoken issuance failed');
                })
                    .finally(() => {
                    inFlightTcTokenIssuance.delete(tcTokenJid);
                });
            }
            // Add message to retry cache if enabled
            if (messageRetryManager && !participant) {
                messageRetryManager.addRecentMessage(destinationJid, msgId, message);
            }
        }, meId);
        return msgId;
    };
    const lidResolver = config.lidResolver || {
        resolve: jid => jid,
        learn: () => undefined
    };
    const resolveSignalJid = (jid) => {
        const cleanJid = sanitizeJid(jid);
        if (typeof cleanJid !== 'string' || !cleanJid.includes('@')) {
            return cleanJid;
        }
        if (/@(g\.us|broadcast|newsletter)$/.test(cleanJid)) {
            return cleanJid;
        }
        const toLid = typeof lidResolver.getLid === 'function' ? lidResolver.getLid : lidResolver.toLid;
        if (typeof toLid !== 'function') {
            return cleanJid;
        }
        const lidJid = sanitizeJid(toLid(cleanJid));
        if (typeof lidJid !== 'string' || !lidJid.endsWith('@lid') || lidJid === cleanJid) {
            return cleanJid;
        }
        return lidJid.replace(/:\d+@/, '@');
    };
    const relayMessage = async (jid, message, options) => rawRelayMessage(
        resolveSignalJid(jid),
        message,
        options
    );
    const getMessageType = (message) => {
        const normalizedMessage = normalizeMessageContent(message);
        if (!normalizedMessage)
            return 'text';
        if (normalizedMessage.reactionMessage || normalizedMessage.encReactionMessage) {
            return 'reaction';
        }
        if (normalizedMessage.pollCreationMessage ||
            normalizedMessage.pollCreationMessageV2 ||
            normalizedMessage.pollCreationMessageV3 ||
            normalizedMessage.pollUpdateMessage) {
            return 'poll';
        }
        if (normalizedMessage.eventMessage) {
            return 'event';
        }
        if (getMediaType(normalizedMessage) !== '') {
            return 'media';
        }
        return 'text';
    };
    const getMediaType = (message) => {
        if (message.imageMessage) {
            return 'image';
        }
        else if (message.videoMessage) {
            return message.videoMessage.gifPlayback ? 'gif' : 'video';
        }
        else if (message.audioMessage) {
            return message.audioMessage.ptt ? 'ptt' : 'audio';
        }
        else if (message.contactMessage) {
            return 'vcard';
        }
        else if (message.documentMessage) {
            return 'document';
        }
        else if (message.contactsArrayMessage) {
            return 'contact_array';
        }
        else if (message.liveLocationMessage) {
            return 'livelocation';
        }
        else if (message.stickerMessage) {
            return 'sticker';
        }
        else if (message.listMessage) {
            return 'list';
        }
        else if (message.listResponseMessage) {
            return 'list_response';
        }
        else if (message.buttonsResponseMessage) {
            return 'buttons_response';
        }
        else if (message.orderMessage) {
            return 'order';
        }
        else if (message.productMessage) {
            return 'product';
        }
        else if (message.interactiveResponseMessage) {
            return 'native_flow_response';
        }
        else if (message.groupInviteMessage) {
            return 'url';
        }
        return '';
    };
    const issuePrivacyTokens = async (jids, timestamp) => {
        const t = (timestamp ?? unixTimestampSeconds()).toString();
        const result = await query({
            tag: 'iq',
            attrs: {
                to: S_WHATSAPP_NET,
                type: 'set',
                xmlns: 'privacy'
            },
            content: [
                {
                    tag: 'tokens',
                    attrs: {},
                    content: jids.map(jid => ({
                        tag: 'token',
                        attrs: {
                            jid: jidNormalizedUser(jid),
                            t,
                            type: 'trusted_contact'
                        }
                    }))
                }
            ]
        });
        return result;
    };
    const waUploadToServer = getWAUploadToServer(config, refreshMediaConn);
    const waitForMsgMediaUpdate = bindWaitForEvent(ev, 'messages.media-update');
    registerSocketEndHandler(() => {
        if (!config.userDevicesCache && userDevicesCache.close) {
            userDevicesCache.close();
        }
        mediaConn = undefined;
        if (messageRetryManager) {
            messageRetryManager.clear();
        }
    });
    const downloadMediaHelper = createDownloadMediaHelper({
        options: {
            options: config.options,
            get host() {
                return mediaHost;
            }
        }
    });
    const sendStatusMention = createStatusMentionHelper({
        sendMessage: (...args) => socket.sendMessage(...args),
        relayMessage: (...args) => socket.relayMessage(...args),
        getGroupMetadata: jid => socket.groupMetadata(jid),
        getOwnJid: () => authState.creds.me?.id,
        getPn: jid => typeof lidResolver.getPn === 'function' ? lidResolver.getPn(jid) : lidResolver.toPn?.(jid) || jid,
        resolveJid: jid => lidResolver.resolve(jid),
        logger
    });
    const hidetag = createHidetagHelper({
        sendMessage: (...args) => socket.sendMessage(...args),
        getGroupMetadata: jid => socket.groupMetadata(jid),
        resolveJid: jid => lidResolver.resolve(jid)
    });
    const readViewOnce = createReadViewOnceHelper({
        downloadMedia: msg => socket.downloadMedia(msg)
    });
    const stripDevice = jid => typeof jid === 'string' ? jid.replace(/:\d+@/, '@') : jid;
    const resolveJid = jid => lidResolver.resolve(jid);
    const richTypes = proto.AIRichResponseSubMessageType || {};
    const richMessage = proto.Message?.RichResponseMessage || proto.AIRichResponseMessage;
    const hasRichMessage = typeof richMessage?.fromObject === 'function';
    const RICH_STANDARD = proto.AIRichResponseMessageType?.AI_RICH_RESPONSE_TYPE_STANDARD ?? 1;

    const toBytes = (value) => {
        if (value == null) return undefined;
        if (Buffer.isBuffer(value)) return value;
        if (value instanceof Uint8Array) return Buffer.from(value);
        if (typeof value === 'string') {
            const compact = value.trim();
            return Buffer.from(compact, /^[A-Za-z0-9+/]+={0,2}$/.test(compact) && compact.length % 4 === 0 ? 'base64' : 'utf8');
        }
        return undefined;
    };

    const buildExternalAdReply = (card) => {
        if (!card || typeof card !== 'object') return null;
        const adReply = {};
        if (card.title != null) adReply.title = String(card.title);
        if (card.body != null) adReply.body = String(card.body);
        const thumbnail = toBytes(card.thumbnail);
        if (thumbnail) adReply.thumbnail = thumbnail;
        if (card.thumbnailUrl) adReply.thumbnailUrl = String(card.thumbnailUrl);
        if (card.sourceUrl) adReply.sourceUrl = String(card.sourceUrl);
        if (card.largeThumbnail) adReply.renderLargerThumbnail = true;
        if (adReply.thumbnail || adReply.thumbnailUrl) {
            adReply.mediaType = proto.ContextInfo.ExternalAdReplyInfo?.MediaType?.IMAGE ?? 1;
        }
        return Object.keys(adReply).length ? adReply : null;
    };

    const toTableRow = (row, index) => {
        const heading = index === 0;
        if (Array.isArray(row)) {
            return { items: row.map(item => String(item)), ...(heading ? { isHeading: true } : {}) };
        }
        if (row && typeof row === 'object') {
            const items = Array.isArray(row.items) ? row.items : Array.isArray(row.cells) ? row.cells : [];
            const isHeading = (row.isHeading ?? heading) === true;
            return { items: items.map(item => String(item)), ...(isHeading ? { isHeading: true } : {}) };
        }
        return { items: [String(row)], ...(heading ? { isHeading: true } : {}) };
    };

    const renderMarkdownTable = (rows, title) => {
        const normalized = rows.map(toTableRow);
        if (!normalized.length) return title ? `**${title}**` : '';
        const width = Math.max(1, ...normalized.map(row => row.items.length));
        const line = row => {
            const cells = [...row.items];
            while (cells.length < width) cells.push('');
            return `| ${cells.join(' | ')} |`;
        };
        const body = [
            line(normalized[0]),
            `| ${Array(width).fill('---').join(' | ')} |`,
            ...normalized.slice(1).map(line)
        ].join('\n');
        return title ? `**${title}**\n${body}` : body;
    };

    const normalizeFlowButtons = (buttons) => (Array.isArray(buttons) ? buttons : []).map((button) => {
        if (typeof button === 'string') return button;
        const requested = button?.type || button?.name || 'quick_reply';
        const type = ['quick_reply', 'cta_copy', 'cta_url'].includes(requested) ? requested : 'quick_reply';
        return { ...button, type, name: type };
    });

    const buildRichBlocks = (blocks) => {
        const submessages = [];
        const markdown = [];
        for (const block of Array.isArray(blocks) ? blocks : []) {
            if (!block || typeof block !== 'object') continue;
            const type = String(block.type || 'text').toLowerCase();
            const content = block.content ?? block.text ?? '';
            try {
                if (type === 'text') {
                    const value = String(content);
                    submessages.push({ messageType: richTypes.AI_RICH_RESPONSE_TEXT ?? 2, messageText: value });
                    markdown.push(value);
                }
                else if (type === 'code') {
                    const value = String(content);
                    const language = block.language ? String(block.language) : '';
                    submessages.push({
                        messageType: richTypes.AI_RICH_RESPONSE_CODE ?? 5,
                        codeMetadata: {
                            ...(language ? { codeLanguage: language } : {}),
                            codeBlocks: [{ codeContent: value }]
                        }
                    });
                    markdown.push(`\`\`\`${language}\n${value}\n\`\`\``);
                }
                else if (type === 'table') {
                    const rows = Array.isArray(block.rows) ? block.rows : [];
                    const title = typeof content === 'string' && content ? content : block.title;
                    submessages.push({
                        messageType: richTypes.AI_RICH_RESPONSE_TABLE ?? 4,
                        tableMetadata: {
                            ...(title ? { title: String(title) } : {}),
                            rows: rows.map(toTableRow)
                        }
                    });
                    markdown.push(renderMarkdownTable(rows, title));
                }
                else if (type === 'image') {
                    const image = typeof content === 'string' ? { url: content } : (content && typeof content === 'object' ? content : {});
                    const url = String(image.url || image.thumbnailUrl || '');
                    submessages.push({
                        messageType: richTypes.AI_RICH_RESPONSE_INLINE_IMAGE ?? 3,
                        imageMetadata: {
                            imageUrl: {
                                imagePreviewUrl: String(image.previewUrl || url),
                                imageHighResUrl: String(image.highResUrl || url),
                                ...(image.sourceUrl ? { sourceUrl: String(image.sourceUrl) } : {})
                            },
                            ...(image.text ? { imageText: String(image.text) } : {}),
                            ...(image.tapLinkUrl ? { tapLinkUrl: String(image.tapLinkUrl) } : {}),
                            ...(image.alignment !== undefined ? { alignment: image.alignment } : {})
                        }
                    });
                    markdown.push(`![${image.text || 'gambar'}](${url})`);
                }
                else {
                    markdown.push(`**${type}**\n${String(content)}`);
                }
            }
            catch {
                markdown.push(String(content));
            }
        }
        return { submessages, markdown: markdown.filter(Boolean) };
    };

    const buildRichContent = ({ blocks = [], card, buttons = [], text } = {}) => {
        const adReply = buildExternalAdReply(card);
        const { submessages, markdown } = buildRichBlocks(blocks);
        const fallbackText = [text, ...markdown].filter(item => item !== '' && item != null).join('\n\n');
        const contextInfo = adReply ? { externalAdReply: adReply } : null;
        const flowButtons = normalizeFlowButtons(buttons);

        if (flowButtons.length) {
            return {
                rich: false,
                content: {
                    text: fallbackText || '',
                    ...(contextInfo ? { contextInfo } : {}),
                    interactiveButtons: flowButtons
                }
            };
        }
        if (hasRichMessage && submessages.length) {
            return {
                rich: true,
                content: {
                    richResponseMessage: {
                        messageType: RICH_STANDARD,
                        submessages,
                        ...(contextInfo ? { contextInfo } : {})
                    }
                }
            };
        }
        return {
            rich: false,
            content: {
                text: fallbackText || '',
                ...(contextInfo ? { contextInfo } : {})
            }
        };
    };

    const sendRichMessage = async (jid, opts = {}, quoted) => {
        const targetJid = stripDevice(resolveJid(jid) || jid);
        const userJid = stripDevice(sock.user?.id || authState.creds.me?.id || '');
        const { content } = buildRichContent(opts);
        const generated = generateWAMessageFromContent(targetJid, content, {
            ...(quoted ? { quoted } : {}),
            userJid
        });
        await socket.relayMessage(targetJid, generated.message, { messageId: generated.key.id });
        return generated;
    };
    const buildGalaxyMessage = ({ text, name = 'galaxy_message', paramsJson, version = 1, quotedMessage, stanzaId, participant }) => {
        const contextInfo = {};
        if (quotedMessage) {
            contextInfo.quotedMessage = quotedMessage;
        }
        if (stanzaId) {
            contextInfo.stanzaId = stanzaId;
        }
        if (participant) {
            contextInfo.participant = sanitizeJid(participant);
        }
        return proto.Message.fromObject({
            interactiveResponseMessage: {
                ...(text ? { body: { text } } : {}),
                nativeFlowResponseMessage: {
                    name,
                    paramsJson: typeof paramsJson === 'string' ? paramsJson : JSON.stringify(paramsJson || {}),
                    version
                },
                ...(Object.keys(contextInfo).length ? { contextInfo } : {})
            }
        });
    };
    const sendGalaxy = async (jid, opts = {}) => {
        const { count = 1, delay: delayMs = 350, participant, stanzaId, quotedMessage, ...rest } = opts;
        const message = buildGalaxyMessage({ ...rest, participant, stanzaId, quotedMessage });
        const total = Math.max(1, Number(count) || 1);
        const ids = [];
        for (let i = 0; i < total; i++) {
            const messageId = generateMessageIDV2(sock.user?.id || authState.creds.me?.id);
            await socket.relayMessage(jid, message, { messageId });
            ids.push(messageId);
            if (i < total - 1 && delayMs > 0) {
                await wait(Number(delayMs));
            }
        }
        return ids;
    };
    const arkanaHelpers = createArkanaHelpers({
        sendMessage: (...args) => socket.sendMessage(...args),
        relayMessage: (...args) => socket.relayMessage(...args),
        prepareMedia: (content, prepareOptions = {}) => prepareWAMessageMedia(content, {
            upload: waUploadToServer,
            mediaCache: config.mediaCache,
            options: config.options,
            logger,
            ...prepareOptions
        }),
        resolveJid: jid => sanitizeJid(typeof lidResolver.resolve === 'function' ? lidResolver.resolve(jid) : jid),
        getPn: jid => typeof lidResolver.getPn === 'function' ? lidResolver.getPn(jid) : lidResolver.toPn?.(jid) || jid,
        userJid: authState.creds.me?.id
    });
    const sendInteractive = async (jid, opts = {}, quoted) => {
        const { text, body, title, headerText, header, footer, image, video, document, buttons = [], name, ...rest } = opts;
        const interactiveButtons = (Array.isArray(buttons) ? buttons : []).map(button => {
            if (typeof button === 'string') {
                return button;
            }
            const type = button.type || button.name || name || 'quick_reply';
            return { ...button, type, name: type };
        });
        const content = {
            ...(text ?? body ? { text: text ?? body } : {}),
            ...(footer ? { footer } : {}),
            ...(title ?? headerText ?? (typeof header === 'string' ? header : header?.title)
                ? { title: title ?? headerText ?? (typeof header === 'string' ? header : header?.title) }
                : {}),
            ...(interactiveButtons.length ? { interactiveButtons } : {}),
            ...(image ? { image } : {}),
            ...(video ? { video } : {}),
            ...(document ? { document } : {}),
            ...rest
        };
        return socket.sendMessage(jid, content, quoted ? { quoted } : {});
    };
    const socket = {
        ...sock,
        userDevicesCache,
        devicesMutex,
        issuePrivacyTokens,
        assertSessions,
        relayMessage,
        sendReceipt,
        sendReceipts,
        readMessages,
        refreshMediaConn,
        // Function (not getter) so the spread in chats.ts preserves the live closure binding.
        getMediaHost: () => mediaHost,
        waUploadToServer,
        fetchPrivacySettings,
        sendPeerDataOperationMessage,
        createParticipantNodes,
        getUSyncDevices,
        messageRetryManager,
        updateMemberLabel,
        updateMediaMessage: async (message) => {
            const content = assertMediaContent(message.message);
            const mediaKey = content.mediaKey;
            const meId = authState.creds.me.id;
            const node = encryptMediaRetryRequest(message.key, mediaKey, meId);
            let error = undefined;
            await Promise.all([
                sendNode(node),
                waitForMsgMediaUpdate(async (update) => {
                    const result = update.find(c => c.key.id === message.key.id);
                    if (result) {
                        if (result.error) {
                            error = result.error;
                        }
                        else {
                            try {
                                const media = decryptMediaRetryData(result.media, mediaKey, result.key.id);
                                if (media.result !== proto.MediaRetryNotification.ResultType.SUCCESS) {
                                    const resultStr = proto.MediaRetryNotification.ResultType[media.result];
                                    throw new Boom(`Media re-upload failed by device (${resultStr})`, {
                                        data: media,
                                        statusCode: getStatusCodeForMediaRetry(media.result) || 404
                                    });
                                }
                                content.directPath = media.directPath;
                                content.url = getUrlFromDirectPath(content.directPath, mediaHost);
                                logger.debug({ directPath: media.directPath, key: result.key }, 'media update successful');
                            }
                            catch (err) {
                                error = err;
                            }
                        }
                        return true;
                    }
                })
            ]);
            if (error) {
                throw error;
            }
            ev.emit('messages.update', [{ key: message.key, update: { message: message.message } }]);
            return message;
        },
        sendMessage: async (jid, content, options = {}) => {
            const userJid = sanitizeJid(authState.creds.me.id);
            jid = sanitizeJid(lidResolver.resolve(jid));
            if (typeof content === 'object' &&
                'disappearingMessagesInChat' in content &&
                typeof content['disappearingMessagesInChat'] !== 'undefined' &&
                isJidGroup(jid)) {
                const { disappearingMessagesInChat } = content;
                const value = typeof disappearingMessagesInChat === 'boolean'
                    ? disappearingMessagesInChat
                        ? WA_DEFAULT_EPHEMERAL
                        : 0
                    : disappearingMessagesInChat;
                await groupToggleEphemeral(jid, value);
            }
            else {
                const isEventMsg = 'event' in content && !!content.event;
                const isDeleteMsg = 'delete' in content && !!content.delete;
                const isEditMsg = 'edit' in content && !!content.edit;
                const isPinMsg = 'pin' in content && !!content.pin;
                const isPollMessage = 'poll' in content && !!content.poll;
                const additionalAttributes = {};
                const additionalNodes = [];
                // required for delete
                if (isDeleteMsg) {
                    // if the chat is a group, and I am not the author, then delete the message as an admin
                    if (isJidGroup(content.delete?.remoteJid) && !content.delete?.fromMe) {
                        additionalAttributes.edit = '8';
                    }
                    else {
                        additionalAttributes.edit = '7';
                    }
                }
                else if (isEditMsg) {
                    additionalAttributes.edit = '1';
                }
                else if (isPinMsg) {
                    additionalAttributes.edit = '2';
                }
                else if (isPollMessage) {
                    additionalNodes.push({
                        tag: 'meta',
                        attrs: {
                            polltype: 'creation'
                        }
                    });
                }
                else if (isEventMsg) {
                    additionalNodes.push({
                        tag: 'meta',
                        attrs: {
                            event_type: 'creation'
                        }
                    });
                }
                const buildMessage = (messageContent, messageIdOverride, generateOverrides = {}) => generateWAMessage(jid, messageContent, {
                    logger,
                    userJid,
                    getUrlInfo: text => getUrlInfo(text, {
                        thumbnailWidth: linkPreviewImageThumbnailWidth,
                        fetchOpts: {
                            timeout: 3000,
                            ...(httpRequestOptions || {})
                        },
                        logger,
                        uploadImage: generateHighQualityLinkPreview ? waUploadToServer : undefined
                    }),
                    getProfilePicUrl: sock.profilePictureUrl,
                    getCallLink: sock.createCallLink,
                    upload: waUploadToServer,
                    mediaCache: config.mediaCache,
                    options: config.options,
                    messageId: generateMessageIDV2(sock.user?.id),
                    ...options,
                    ...generateOverrides,
                    ...(messageIdOverride ? { messageId: messageIdOverride } : {})
                });
                const sendGeneratedMessage = async (messageContent, relayOptions = {}, messageIdOverride, generateOverrides = {}) => {
                    const generated = applyContextPatch(
                        await buildMessage(messageContent, messageIdOverride, generateOverrides),
                        options
                    );
                    await socket.relayMessage(jid, generated.message, {
                        messageId: generated.key.id,
                        useCachedGroupMetadata: options.useCachedGroupMetadata,
                        statusJidList: options.statusJidList,
                        ...relayOptions
                    });
                    if (config.emitOwnEvents) {
                        process.nextTick(async () => {
                            await messageMutex.mutex(() => upsertMessage(generated, 'append'));
                        });
                    }
                    return generated;
                };
                if (options.fakeQuote) {
                    const fake = options.fakeQuote;
                    const cleanContent = unwrapMessageContent(content) || content;
                    const { quoted } = buildFakeQuote(fake, jid);
                    return sendGeneratedMessage(
                        cleanContent,
                        { additionalAttributes, additionalNodes },
                        options.messageId,
                        { quoted }
                    );
                }
                if (Array.isArray(content?.album)) {
                    const album = content.album;
                    if (album.length < 2) {
                        throw new Boom('album requires at least 2 media items', { statusCode: 400 });
                    }
                    const counts = album.reduce((acc, item) => {
                        if (item?.image) {
                            acc.expectedImageCount += 1;
                        }
                        else if (item?.video) {
                            acc.expectedVideoCount += 1;
                        }
                        else {
                            throw new Boom('album items must contain an image or a video', { statusCode: 400 });
                        }
                        return acc;
                    }, { expectedImageCount: 0, expectedVideoCount: 0 });
                    const parentContent = { ...content, album: counts };
                    delete parentContent.text;
                    const parentMsg = await sendGeneratedMessage(parentContent, {
                        additionalAttributes,
                        additionalNodes: [...additionalNodes]
                    });
                    for (const item of album) {
                        await sendGeneratedMessage({
                            ...item,
                            albumParentKey: parentMsg.key
                        }, {}, generateMessageIDV2(sock.user?.id));
                    }
                    return parentMsg;
                }
                return sendGeneratedMessage(content, { additionalAttributes, additionalNodes });
            }
        },
        downloadMedia: downloadMediaHelper,
        sendStatusMention,
        hidetag,
        readViewOnce,
        sendGalaxy,
        sendInteractive,
        sendRichMessage,
        getPn: jid => typeof lidResolver.getPn === 'function' ? lidResolver.getPn(jid) : lidResolver.toPn?.(jid) || jid,
        getLid: jid => typeof lidResolver.getLid === 'function' ? lidResolver.getLid(jid) : lidResolver.toLid?.(jid) || jid,
        signalJid: resolveSignalJid,
        ...arkanaHelpers
    };
    socket.reply = createReplyHelper(socket.sendMessage);
    return socket;
};
//# sourceMappingURL=messages-send.js.map