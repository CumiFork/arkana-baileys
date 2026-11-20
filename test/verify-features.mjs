import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { aesEncryptGCM, createDownloadMediaHelper, createLidResolver, createMessageStore, createReplyHelper, configurePollContext, extractMessage, generateWAMessageContent, generateWAMessageFromContent, getContentType, hmacSign, normalizeMessageContent, proto, sha256 } from 'arkana-baileys';

const root = fileURLToPath(new URL('..', import.meta.url));
const jid = '6281234567890@s.whatsapp.net';
const media = Buffer.from('arkana-media-test');
const thumbnail = Buffer.from('arkana-thumbnail-test');
const upload = async () => ({ mediaUrl: 'https://example.invalid/media', directPath: '/mock/media' });
const options = { jid, upload };
const encode = content => proto.Message.encode(content).finish();
const roundtrip = content => proto.Message.decode(encode(content));

const interactive = await generateWAMessageContent({
    image: media,
    jpegThumbnail: thumbnail,
    title: 'Promo',
    subtitle: 'Hari ini',
    text: 'Pilih @6281111111111',
    footer: 'Arkana Store',
    interactiveButtons: [{
        name: 'quick_reply',
        buttonParamsJson: { display_text: 'Oke', id: 'oke' }
    }]
}, options);
const decodedInteractive = roundtrip(interactive);
assert.equal(decodedInteractive.interactiveMessage.header.hasMediaAttachment, true);
assert.ok(decodedInteractive.interactiveMessage.header.imageMessage);
assert.equal(decodedInteractive.interactiveMessage.header.title, 'Promo');
assert.equal(decodedInteractive.interactiveMessage.header.subtitle, 'Hari ini');
assert.equal(decodedInteractive.interactiveMessage.body.text, 'Pilih @6281111111111');
assert.equal(decodedInteractive.interactiveMessage.footer.text, 'Arkana Store');
assert.deepEqual(decodedInteractive.interactiveMessage.contextInfo.mentionedJid, ['6281111111111@s.whatsapp.net']);

const protoSource = readFileSync(join(root, 'WAProto', 'index.proto'), 'utf8');
for (const declaration of [
    'optional PollCreationMessage pollCreationMessageV3 = 64;',
    'optional EventMessage eventMessage = 75;',
    'optional bool extraGuestsAllowed = 9;',
    'optional ForwardedNewsletterMessageInfo forwardedNewsletterMessageInfo = 43;',
    'optional uint32 expiration = 25;',
    'optional MessageAssociation messageAssociation = 10;',
    'optional string accessibilityLabel = 10;',
    'optional string flowId = 4;'
]) {
    assert.ok(protoSource.includes(declaration), `missing proto declaration: ${declaration}`);
}

const fieldsInMessage = (declaration) => {
    const start = protoSource.indexOf(declaration);
    assert.notEqual(start, -1, `missing message: ${declaration}`);
    let cursor = start + declaration.length - 1;
    let depth = 0;
    const fields = [];
    while (cursor < protoSource.length) {
        const char = protoSource[cursor];
        if (char === '{') {
            depth += 1;
        }
        else if (char === '}') {
            depth -= 1;
            if (depth === 0) {
                break;
            }
        }
        else if (depth === 1) {
            const match = /^\s*(?:optional|required|repeated)\s+[\w.<>]+\s+(\w+)\s*=\s*(\d+)\s*;/.exec(protoSource.slice(cursor));
            if (match) {
                fields.push(Number(match[2]));
                cursor += match[0].length - 1;
            }
        }
        cursor += 1;
    }
    return fields;
};
for (const declaration of ['message EventMessage {', 'message MessageContextInfo {', 'message ContextInfo {']) {
    const numbers = fieldsInMessage(declaration);
    assert.equal(new Set(numbers).size, numbers.length, `duplicate proto tags in ${declaration}`);
}

const extendedProto = proto.Message.fromObject({
    interactiveMessage: {
        header: { title: 'Header', accessibilityLabel: 'label' },
        nativeFlowMessage: {
            messageVersion: 3,
            flowId: 'flow-1',
            buttons: [{ name: 'quick_reply', buttonParamsJson: '{}' }]
        }
    },
    pollCreationMessageV3: {
        name: 'Proto poll',
        selectableOptionsCount: 1,
        options: [{ optionName: 'A' }]
    },
    eventMessage: { name: 'Proto event', startTime: 1700000000, extraGuestsAllowed: true },
    extendedTextMessage: {
        text: 'x',
        contextInfo: {
            isForwarded: true,
            forwardingScore: 2,
            expiration: 86400,
            forwardedNewsletterMessageInfo: {
                newsletterJid: '120363012345678901@newsletter',
                newsletterName: 'Proto News',
                serverMessageId: 7
            }
        }
    },
    messageContextInfo: {
        messageAssociation: {
            associationType: proto.MessageAssociation.AssociationType.MEDIA_ALBUM,
            parentMessageKey: { id: 'parent-1' }
        }
    }
});
const decodedProto = roundtrip(extendedProto);
assert.equal(decodedProto.interactiveMessage.header.accessibilityLabel, 'label');
assert.equal(decodedProto.interactiveMessage.nativeFlowMessage.flowId, 'flow-1');
assert.equal(decodedProto.pollCreationMessageV3.options[0].optionName, 'A');
assert.equal(decodedProto.eventMessage.extraGuestsAllowed, true);
assert.equal(Number(decodedProto.eventMessage.startTime), 1700000000);
assert.equal(decodedProto.extendedTextMessage.contextInfo.expiration, 86400);
assert.equal(decodedProto.extendedTextMessage.contextInfo.forwardedNewsletterMessageInfo.serverMessageId, 7);
assert.equal(decodedProto.messageContextInfo.messageAssociation.associationType, proto.MessageAssociation.AssociationType.MEDIA_ALBUM);

const pollShortcut = roundtrip(await generateWAMessageContent({
    poll: { name: 'Pilihan', values: ['A', 'B'] }
}, options));
assert.ok(pollShortcut.pollCreationMessageV3);
assert.equal(pollShortcut.pollCreationMessageV3.name, 'Pilihan');
assert.equal(pollShortcut.pollCreationMessageV3.options.length, 2);
assert.equal(pollShortcut.pollCreationMessageV3.selectableOptionsCount, 0);
assert.equal(pollShortcut.messageContextInfo.messageSecret.length, 32);

const singleSelectPoll = roundtrip(await generateWAMessageContent({
    poll: { name: 'Satu', values: ['A', 'B'], selectableCount: 1 }
}, options));
assert.ok(singleSelectPoll.pollCreationMessageV3);
assert.equal(singleSelectPoll.pollCreationMessageV3.selectableOptionsCount, 1);

const eventShortcut = roundtrip(await generateWAMessageContent({
    event: {
        name: 'Acara',
        description: 'Deskripsi',
        startTime: 1700000000,
        endTime: 1700003600,
        extraGuestsAllowed: true,
        isCanceled: false
    }
}, options));
assert.equal(eventShortcut.eventMessage.name, 'Acara');
assert.equal(Number(eventShortcut.eventMessage.startTime), 1700000000);
assert.equal(Number(eventShortcut.eventMessage.endTime), 1700003600);
assert.equal(eventShortcut.eventMessage.extraGuestsAllowed, true);
assert.equal(eventShortcut.messageContextInfo.messageSecret.length, 32);

const legacyEvent = roundtrip(await generateWAMessageContent({
    event: { name: 'Lama', startDate: new Date(1700000000000) }
}, options));
assert.equal(Number(legacyEvent.eventMessage.startTime), 1700000000);

const replyCalls = [];
const reply = createReplyHelper(async (...args) => {
    replyCalls.push(args);
    return 'sent';
});
const quotedReply = { key: { id: 'quoted-1' } };
assert.equal(await reply(jid, 'Balasan', quotedReply, { ephemeralExpiration: 86400 }), 'sent');
assert.deepEqual(replyCalls[0], [
    jid,
    { text: 'Balasan' },
    { quoted: quotedReply, ephemeralExpiration: 86400 }
]);

let downloadInput;
const downloadMedia = createDownloadMediaHelper({
    options: { options: {} },
    download: async (message, type, downloadOptions) => {
        downloadInput = { message, type, downloadOptions };
        return Buffer.from('arkana-download');
    }
});
const imageContent = proto.Message.fromObject({ imageMessage: { url: 'https://example.invalid/media', directPath: '/mock/media' } });
const wrappedImage = proto.Message.fromObject({
    ephemeralMessage: {
        message: proto.Message.fromObject({
            viewOnceMessageV2: { message: imageContent }
        })
    }
});
const downloaded = await downloadMedia({ key: { id: 'media-1' }, message: wrappedImage });
assert.equal(downloaded.toString(), 'arkana-download');
assert.equal(downloadInput.type, 'buffer');
assert.ok(downloadInput.message.message.imageMessage);

const quotedImage = { key: { id: 'quoted-2' }, quotedMessage: { key: { id: 'quoted-2' }, message: imageContent } };
await downloadMedia(quotedImage);
assert.ok(downloadInput.message.message.imageMessage);
await assert.rejects(
    () => downloadMedia({ message: proto.Message.fromObject({ conversation: 'bukan media' }) }),
    /does not contain media/
);

const albumItems = [
    { image: media, jpegThumbnail: thumbnail, caption: 'Pertama' },
    { video: media, jpegThumbnail: thumbnail, caption: 'Kedua' }
];
const albumParent = await generateWAMessageContent({ album: albumItems }, options);
const parentMsg = generateWAMessageFromContent(jid, albumParent, { userJid: jid });
assert.equal(roundtrip(albumParent).albumMessage.expectedImageCount, 1);
assert.equal(roundtrip(albumParent).albumMessage.expectedVideoCount, 1);
encode(parentMsg.message);
const albumChild = await generateWAMessageContent({
    image: media,
    jpegThumbnail: thumbnail,
    caption: 'Anak album',
    albumParentKey: parentMsg.key
}, options);
const decodedChild = roundtrip(albumChild);
assert.equal(decodedChild.messageContextInfo.messageAssociation.associationType, proto.MessageAssociation.AssociationType.MEDIA_ALBUM);
assert.deepEqual(decodedChild.messageContextInfo.messageAssociation.parentMessageKey, parentMsg.key);

const textContent = await generateWAMessageContent({
    text: 'Halo @6281234567890 dan @55555, cc @6281234567890',
    mentions: ['6289999999999@s.whatsapp.net']
}, options);
assert.deepEqual(roundtrip(textContent).extendedTextMessage.contextInfo.mentionedJid, [
    '6289999999999@s.whatsapp.net',
    '6281234567890@s.whatsapp.net',
    '55555@s.whatsapp.net'
]);
const captionContent = await generateWAMessageContent({
    image: media,
    jpegThumbnail: thumbnail,
    caption: 'Foto @6287777777777'
}, options);
assert.deepEqual(roundtrip(captionContent).imageMessage.contextInfo.mentionedJid, ['6287777777777@s.whatsapp.net']);

const quoted = generateWAMessageFromContent(jid, proto.Message.fromObject({ conversation: 'pesan lama' }), { userJid: jid });
const forwardedContent = await generateWAMessageContent({
    text: 'Diteruskan',
    isForwarded: true,
    channelBadge: {
        jid: '120363012345678901@newsletter',
        name: 'Arkana News',
        serverMessageId: 42
    },
    contextInfo: { conversionSource: 'arkana-test' }
}, options);
const forwardedMsg = generateWAMessageFromContent(jid, forwardedContent, { userJid: jid, quoted });
const decodedForwarded = roundtrip(forwardedMsg.message).extendedTextMessage.contextInfo;
assert.equal(decodedForwarded.isForwarded, true);
assert.equal(decodedForwarded.forwardingScore, 999);
assert.equal(decodedForwarded.conversionSource, 'arkana-test');
assert.equal(decodedForwarded.forwardedNewsletterMessageInfo.newsletterJid, '120363012345678901@newsletter');
assert.equal(decodedForwarded.forwardedNewsletterMessageInfo.newsletterName, 'Arkana News');
assert.equal(decodedForwarded.forwardedNewsletterMessageInfo.serverMessageId, 42);
assert.ok(decodedForwarded.quotedMessage);

const plainForwarded = await generateWAMessageContent({ text: 'Forward biasa', isForwarded: true }, options);
assert.equal(roundtrip(plainForwarded).extendedTextMessage.contextInfo.forwardingScore, 1);

const videoNote = await generateWAMessageContent({
    videoNote: media,
    jpegThumbnail: thumbnail,
    caption: 'Catatan video'
}, options);
assert.ok(roundtrip(videoNote).ptvMessage);

const voiceNote = await generateWAMessageContent({
    audio: media,
    ptt: true,
    viewOnce: true,
    seconds: 5,
    waveform: Buffer.alloc(0)
}, options);
const decodedVoice = roundtrip(voiceNote);
assert.ok(decodedVoice.viewOnceMessageV2);
assert.equal(decodedVoice.viewOnceMessageV2.message.audioMessage.viewOnce, true);
assert.equal(normalizeMessageContent(decodedVoice).audioMessage.ptt, true);
encode(voiceNote);

const stickerViewOnce = await generateWAMessageContent({ sticker: media, viewOnce: true }, options);
assert.equal(stickerViewOnce.viewOnceMessageV2.message.stickerMessage.viewOnce, true);
assert.ok(roundtrip(stickerViewOnce).viewOnceMessageV2.message.stickerMessage);

for (const wrapper of [
    { ephemeralMessage: { message: proto.Message.fromObject({ conversation: 'halo' }) } },
    { viewOnceMessage: { message: proto.Message.fromObject({ conversation: 'halo' }) } },
    { viewOnceMessageV2: { message: proto.Message.fromObject({ conversation: 'halo' }) } },
    { viewOnceMessageV2Extension: { message: proto.Message.fromObject({ conversation: 'halo' }) } },
    { documentWithCaptionMessage: { message: proto.Message.fromObject({ conversation: 'halo' }) } },
    { editedMessage: { message: proto.Message.fromObject({ conversation: 'halo' }) } }
]) {
    assert.equal(extractMessage({ message: wrapper }).text, 'halo');
}

const withQuote = await generateWAMessageContent({
    text: 'Mengutip',
    contextInfo: { quotedMessage: proto.Message.fromObject({ conversation: 'dikutip' }) }
}, options);
assert.equal(extractMessage({ message: withQuote }).quoted.text, 'dikutip');

const nativeFlowReply = proto.Message.fromObject({
    interactiveResponseMessage: {
        nativeFlowResponseMessage: {
            name: 'quick_reply',
            paramsJson: JSON.stringify({ id: 'pilih_1', display_text: 'Pilih satu' })
        }
    }
});
assert.deepEqual(
    (({ type, text, selectedId, flowParams }) => ({ type, text, selectedId, flowParams }))(extractMessage({ message: nativeFlowReply })),
    {
        type: 'interactiveResponseMessage',
        text: 'Pilih satu',
        selectedId: 'pilih_1',
        flowParams: { id: 'pilih_1', display_text: 'Pilih satu' }
    }
);

for (const [content, expectedId] of [
    [{ listResponseMessage: { title: 'Daftar', singleSelectReply: { selectedRowId: 'baris_2' } } }, 'baris_2'],
    [{ buttonsResponseMessage: { selectedButtonId: 'tombol_1', selectedDisplayText: 'Tombol' } }, 'tombol_1'],
    [{ templateButtonReplyMessage: { selectedId: 'template_1', selectedDisplayText: 'Template' } }, 'template_1']
]) {
    const extracted = extractMessage({ message: proto.Message.fromObject(content) });
    assert.equal(extracted.selectedId, expectedId);
    assert.ok(extracted.text);
}

const store = createMessageStore(2);
store.put({ key: { remoteJid: jid, id: 'store-a' } });
store.put({ key: { remoteJid: jid, id: 'store-b' } });
store.put({ key: { remoteJid: jid, id: 'store-c' } });
assert.equal(store.size, 2);
assert.equal(store.get({ id: 'store-a' }), undefined);
assert.ok(store.get({ id: 'store-c' }));

const resolver = createLidResolver();
resolver.learn('163630452625506@lid', '6281234567890@s.whatsapp.net');
assert.equal(resolver.resolve('163630452625506@lid'), '6281234567890@s.whatsapp.net');
assert.equal(resolver.resolve('163630452625506:9@lid'), '6281234567890:9@s.whatsapp.net');

const extendedButtons = roundtrip(await generateWAMessageContent({
    text: 'Tombol tambahan',
    interactiveButtons: [
        { type: 'cta_copy', display_text: 'Salin', copy_code: 'ARKANA50', id: 'copy' },
        { type: 'cta_call', display_text: 'Telepon', phone_number: '+6281234567890' },
        { type: 'send_location', display_text: 'Lokasi' }
    ]
}, options));
assert.deepEqual(
    extendedButtons.interactiveMessage.nativeFlowMessage.buttons.map(button => button.name),
    ['cta_copy', 'cta_call', 'send_location']
);
assert.equal(
    JSON.parse(extendedButtons.interactiveMessage.nativeFlowMessage.buttons[0].buttonParamsJson).copy_code,
    'ARKANA50'
);

const paymentShortcut = roundtrip(await generateWAMessageContent({
    requestPayment: { currency: 'IDR', amount: 50000, note: 'Tagihan' }
}, options));
assert.equal(paymentShortcut.requestPaymentMessage.currencyCodeIso4217, 'IDR');
assert.equal(Number(paymentShortcut.requestPaymentMessage.amount1000), 50000000);
assert.equal(paymentShortcut.requestPaymentMessage.noteMessage.extendedTextMessage.text, 'Tagihan');

const pollForVote = generateWAMessageFromContent(jid, await generateWAMessageContent({
    poll: { name: 'Vote', values: ['A', 'B'], selectableCount: 1 }
}, options), { userJid: jid });
configurePollContext({ getMessage: store.get, getOwnJid: () => jid });
const voterJid = '6281111111111@s.whatsapp.net';
const pollMsgId = pollForVote.key.id;
const secret = pollForVote.message.messageContextInfo.messageSecret;
const sign = Buffer.concat([
    Buffer.from(pollMsgId),
    Buffer.from(jid),
    Buffer.from(voterJid),
    Buffer.from('Poll Vote'),
    new Uint8Array([1])
]);
const key0 = hmacSign(secret, new Uint8Array(32), 'sha256');
const decKey = hmacSign(sign, key0, 'sha256');
const iv = Buffer.alloc(12);
const plaintext = proto.Message.PollVoteMessage.encode({
    selectedOptions: [sha256(Buffer.from('A'))]
}).finish();
const encPayload = aesEncryptGCM(plaintext, decKey, iv, Buffer.from(`${pollMsgId}\u0000${voterJid}`));
const voteMsg = {
    key: { remoteJid: jid, participant: voterJid, id: 'vote-1' },
    message: proto.Message.fromObject({
        pollUpdateMessage: {
            pollCreationMessageKey: pollForVote.key,
            vote: { encPayload, encIv: iv },
            senderTimestampMs: 1
        }
    })
};
store.put(pollForVote);
store.put(voteMsg);
assert.deepEqual(extractMessage(voteMsg).pollVotes, ['A']);

const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
assert.equal(pkg.version, '1.3.0-mod');
assert.equal(pkg.scripts.prepare, undefined);
const walk = dir => readdirSync(dir).flatMap(entry => {
    const path = join(dir, entry);
    return statSync(path).isDirectory() ? walk(path) : [path];
});
for (const file of walk(join(root, 'lib')).filter(path => path.endsWith('.js'))) {
    assert.doesNotMatch(readFileSync(file, 'utf8'), /\brequire\s*\(/, file);
}
const waProtoJs = readFileSync(join(root, 'WAProto', 'index.js'), 'utf8');
assert.match(waProtoJs, /^import \* as \$protobuf from "protobufjs\/minimal\.js";/m);
assert.doesNotMatch(waProtoJs, /\brequire\s*\(/);

assert.equal(getContentType(proto.Message.fromObject({ conversation: 'x' })), 'conversation');
console.log('verify-features: all checks passed');
