import { Boom } from '@hapi/boom';
import { proto } from '../../WAProto/index.js';
import { generateWAMessageFromContent } from './messages.js';

const MEDIA_WRAPPERS = [
    'viewOnceMessage',
    'viewOnceMessageV2',
    'viewOnceMessageV2Extension',
    'ephemeralMessage',
    'documentWithCaptionMessage'
];

export const ARKANA_MEDIA_HOLDERS = Object.freeze({
    imageMessage: 'image',
    videoMessage: 'video',
    audioMessage: 'audio',
    pttMessage: 'audio',
    stickerMessage: 'sticker',
    documentMessage: 'document'
});

export const stripArkanaDevice = (jid) => typeof jid === 'string' ? jid.replace(/:\d+@/, '@') : jid;

export const unwrapArkanaMessage = (message) => {
    let current = message;
    for (let index = 0; index < 12 && current; index += 1) {
        const wrapperKey = MEDIA_WRAPPERS.find(key => current[key]?.message);
        if (!wrapperKey) {
            break;
        }
        current = current[wrapperKey].message;
    }
    return current;
};

const toUnixSeconds = (value) => {
    if (value == null) {
        return undefined;
    }
    if (value instanceof Date) {
        return Math.floor(value.getTime() / 1000);
    }
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
        return undefined;
    }
    return numeric > 1e12 ? Math.floor(numeric / 1000) : Math.floor(numeric);
};

const normalizeButton = (button) => {
    if (typeof button === 'string') {
        return { name: 'quick_reply', buttonParamsJson: JSON.stringify({ display_text: button, id: button }) };
    }
    if (!button || typeof button !== 'object') {
        return null;
    }
    if (button.name) {
        return button;
    }
    return {
        name: button.type || 'quick_reply',
        buttonParamsJson: JSON.stringify({
            display_text: button.displayText || button.label || button.text || '',
            id: button.id || button.label || button.text || ''
        })
    };
};

export const buildAdReplyContext = (adOptions = {}) => {
    const mediaType = typeof adOptions.mediaType === 'string'
        ? ({ image: 1, video: 2 }[adOptions.mediaType.toLowerCase()] ?? 0)
        : Number(adOptions.mediaType) || 0;
    const adReply = {
        title: adOptions.title,
        body: adOptions.body,
        mediaType,
        thumbnailUrl: adOptions.thumbnailUrl,
        mediaUrl: adOptions.mediaUrl,
        thumbnail: adOptions.thumbnail,
        sourceUrl: adOptions.sourceUrl,
        renderLargerThumbnail: Boolean(adOptions.renderLargerThumbnail)
    };
    return Object.fromEntries(Object.entries(adReply).filter(([, value]) => value !== undefined));
};

export const buildCarouselContent = async (options = {}, prepareMedia) => {
    const { text, footer, title, cards = [], contextInfo } = options;
    if (!Array.isArray(cards) || cards.length === 0) {
        throw new Boom('sendCarousel needs at least one card', { statusCode: 400 });
    }
    if (typeof prepareMedia !== 'function') {
        throw new Boom('sendCarousel needs a media preparer', { statusCode: 500 });
    }
    const normalizedCards = await Promise.all(cards.map(async (card, index) => {
        if (!card || typeof card !== 'object') {
            throw new Boom(`Carousel card ${index} is invalid`, { statusCode: 400 });
        }
        const mediaType = card.image ? 'image' : card.video ? 'video' : null;
        let header;
        if (mediaType) {
            const prepared = await prepareMedia({
                [mediaType]: card.image || card.video,
                ...(card.caption ? { caption: card.caption } : {})
            });
            header = {
                title: card.title || title || '',
                hasMediaAttachment: true,
                [`${mediaType}Message`]: prepared[`${mediaType}Message`]
            };
        }
        else if (card.title || title) {
            header = { title: card.title || title };
        }
        const buttons = (card.buttons || []).map(normalizeButton).filter(Boolean);
        const nativeFlowMessage = {
            buttons,
            messageVersion: card.messageVersion ?? 3
        };
        if (card.messageParamsJson) {
            nativeFlowMessage.messageParamsJson = card.messageParamsJson;
        }
        return {
            ...(header ? { header } : {}),
            body: { text: card.body ?? card.text ?? '' },
            ...(card.footer ? { footer: { text: card.footer } } : {}),
            ...(contextInfo ? { contextInfo } : {}),
            nativeFlowMessage
        };
    }));
    return {
        interactiveMessage: {
            ...(title && !normalizedCards.some(card => card.header) ? { header: { title } } : {}),
            ...(text ? { body: { text } } : {}),
            ...(footer ? { footer: { text: footer } } : {}),
            ...(contextInfo ? { contextInfo } : {}),
            carouselMessage: {
                cards: normalizedCards,
                messageVersion: 1
            }
        }
    };
};

export const buildAlbumContent = async (medias = [], prepareMedia) => {
    if (!Array.isArray(medias) || medias.length === 0) {
        throw new Boom('sendAlbum needs at least one media item', { statusCode: 400 });
    }
    if (typeof prepareMedia !== 'function') {
        throw new Boom('sendAlbum needs a media preparer', { statusCode: 500 });
    }
    const children = await Promise.all(medias.map(async (media, index) => {
        if (!media || typeof media !== 'object') {
            throw new Boom(`Album item ${index} is invalid`, { statusCode: 400 });
        }
        const holderKey = Object.keys(ARKANA_MEDIA_HOLDERS).find(key => media[ARKANA_MEDIA_HOLDERS[key]]);
        if (!holderKey) {
            throw new Boom(`Album item ${index} has no supported media`, { statusCode: 400 });
        }
        return prepareMedia({
            [holderKey]: media[ARKANA_MEDIA_HOLDERS[holderKey]],
            ...(media.caption ? { caption: media.caption } : {}),
            ...(media.mimetype ? { mimetype: media.mimetype } : {}),
            ...(media.fileName ? { fileName: media.fileName } : {})
        });
    }));
    return {
        albumMessage: {
            expectedImageCount: children.filter(child => child.imageMessage).length,
            expectedVideoCount: children.filter(child => child.videoMessage).length
        },
        children
    };
};

export const buildPollV3Content = ({ name, values = [], selectableCount = 1, ...rest } = {}) => {
    if (!name || !Array.isArray(values) || values.length === 0) {
        throw new Boom('sendPollV3 needs name and values', { statusCode: 400 });
    }
    return {
        pollCreationMessageV3: {
            name,
            options: values.map(value => typeof value === 'string'
                ? { optionName: value }
                : { optionName: value.optionName ?? value.name ?? String(value) }),
            selectableOptionsCount: selectableCount,
            ...rest
        }
    };
};

export const buildEventContent = ({ name, description, startTime, endTime, location, joinLink, ...rest } = {}) => {
    if (!name) {
        throw new Boom('sendEvent needs a name', { statusCode: 400 });
    }
    return {
        eventMessage: {
            name,
            ...(description ? { description } : {}),
            ...(location ? { location } : {}),
            ...(joinLink ? { joinLink } : {}),
            ...(toUnixSeconds(startTime) !== undefined ? { startTime: toUnixSeconds(startTime) } : {}),
            ...(toUnixSeconds(endTime) !== undefined ? { endTime: toUnixSeconds(endTime) } : {}),
            ...rest
        }
    };
};

export const buildPaymentRequestContent = ({ amount, currency, note, expiry, background, amount1000, ...rest } = {}) => {
    const numericAmount = Number(amount);
    if (!Number.isFinite(numericAmount) && amount1000 === undefined) {
        throw new Boom('sendPaymentRequest needs an amount', { statusCode: 400 });
    }
    return {
        requestPaymentMessage: {
            amount1000: amount1000 ?? Math.round(numericAmount * 1000),
            ...(currency ? { currencyCodeIso4217: currency } : {}),
            ...(note ? { noteMessage: note } : {}),
            ...(expiry !== undefined ? { expiryTimestamp: toUnixSeconds(expiry) } : {}),
            ...(background !== undefined ? { background } : {}),
            ...rest
        }
    };
};

export const buildOrderContent = (orderData = {}) => {
    if (orderData.product) {
        return { productMessage: orderData.product };
    }
    if (!orderData.orderId && !orderData.itemCount) {
        throw new Boom('sendOrder needs orderId or itemCount', { statusCode: 400 });
    }
    return {
        orderMessage: {
            orderId: orderData.orderId,
            itemCount: orderData.itemCount,
            ...(orderData.thumbnail ? { thumbnail: orderData.thumbnail } : {}),
            ...(orderData.status !== undefined ? { status: orderData.status } : {}),
            ...(orderData.surface !== undefined ? { surface: orderData.surface } : {}),
            ...(orderData.message ? { message: orderData.message } : {}),
            ...(orderData.orderTitle ? { orderTitle: orderData.orderTitle } : {}),
            ...(orderData.sellerJid ? { sellerJid: orderData.sellerJid } : {}),
            ...(orderData.token ? { token: orderData.token } : {}),
            ...(orderData.totalAmount1000 !== undefined ? { totalAmount1000: orderData.totalAmount1000 } : {}),
            ...(orderData.totalCurrencyCode ? { totalCurrencyCode: orderData.totalCurrencyCode } : {}),
            ...(orderData.contextInfo ? { contextInfo: orderData.contextInfo } : {})
        }
    };
};

export const buildAdReplyContent = (text, adOptions = {}) => {
    const contextInfo = {
        ...(adOptions.contextInfo || {}),
        externalAdReply: buildAdReplyContext(adOptions)
    };
    if (typeof text === 'string') {
        return { text, contextInfo };
    }
    return {
        ...(text || {}),
        contextInfo: {
            ...((text || {}).contextInfo || {}),
            ...contextInfo
        }
    };
};

export const createArkanaHelpers = ({
    sendMessage,
    relayMessage,
    prepareMedia,
    resolveJid = jid => jid,
    getPn = jid => jid,
    userJid
} = {}) => {
    const normalizeTarget = jid => stripArkanaDevice(resolveJid(jid) || jid);
    const generate = (jid, content, options = {}) => generateWAMessageFromContent(normalizeTarget(jid), content, {
        ...(userJid ? { userJid } : {}),
        ...options
    });

    const sendGenerated = async (jid, content, options = {}, relayOptions = {}) => {
        const generated = generate(jid, content, options);
        await relayMessage(normalizeTarget(jid), generated.message, {
            messageId: generated.key.id,
            ...relayOptions
        });
        return generated;
    };

    const sendCarousel = async (jid, options = {}, sendOptions = {}) => {
        const content = await buildCarouselContent(options, prepareMedia);
        return sendMessage(jid, content, sendOptions);
    };

    const sendAlbum = async (jid, medias = [], options = {}) => {
        const { children, albumMessage } = await buildAlbumContent(medias, prepareMedia);
        const parent = await sendGenerated(jid, { albumMessage }, options);
        const childIds = [];
        for (const child of children) {
            const generatedChild = generate(jid, child, options);
            await relayMessage(normalizeTarget(jid), {
                associatedChildMessage: { message: generatedChild.message }
            }, { messageId: generatedChild.key.id });
            childIds.push(generatedChild.key.id);
        }
        return { ...parent, childIds };
    };

    const sendPollV3 = (jid, options = {}, sendOptions = {}) => sendMessage(jid, buildPollV3Content(options), sendOptions);
    const sendEvent = (jid, options = {}, sendOptions = {}) => sendMessage(jid, buildEventContent(options), sendOptions);
    const sendPaymentRequest = (jid, options = {}, sendOptions = {}) => sendMessage(jid, buildPaymentRequestContent(options), sendOptions);
    const sendOrder = (jid, orderData = {}, sendOptions = {}) => sendMessage(jid, buildOrderContent(orderData), sendOptions);
    const sendViewOnce = (jid, mediaContent = {}, sendOptions = {}) => sendMessage(jid, {
        ...mediaContent,
        viewOnce: true
    }, sendOptions);
    const sendAdReply = (jid, text, adOptions = {}, sendOptions = {}) => sendMessage(
        jid,
        buildAdReplyContent(text, adOptions),
        sendOptions
    );

    const resolveStatusTargets = (jids = []) => [...new Set((jids || [])
        .map(jid => stripArkanaDevice(getPn(jid) || jid))
        .filter(jid => typeof jid === 'string' && jid.endsWith('@s.whatsapp.net')))];

    return {
        sendCarousel,
        sendAlbum,
        sendPollV3,
        sendEvent,
        sendPaymentRequest,
        sendOrder,
        sendViewOnce,
        sendAdReply,
        resolveStatusTargets,
        buildCarouselContent: options => buildCarouselContent(options, prepareMedia),
        buildAlbumContent: medias => buildAlbumContent(medias, prepareMedia),
        buildPollV3Content,
        buildEventContent,
        buildPaymentRequestContent,
        buildOrderContent,
        buildAdReplyContent
    };
};

export default createArkanaHelpers;
