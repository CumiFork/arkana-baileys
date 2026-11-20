const normalizeUser = (jid) => String(jid || '').split('@')[0].split(':')[0];

const asPnJid = (jid) => {
    if (typeof jid !== 'string' || !jid.includes('@')) {
        return '';
    }
    const [, domain] = jid.split('@');
    if (domain !== 's.whatsapp.net' && domain !== 'c.us') {
        return '';
    }
    const user = normalizeUser(jid);
    return /^\d+$/.test(user) ? `${user}@s.whatsapp.net` : '';
};

const asLidJid = (jid) => {
    if (typeof jid !== 'string' || !jid.includes('@')) {
        return '';
    }
    const [, domain] = jid.split('@');
    if (domain !== 'lid' && domain !== 'hosted.lid') {
        return '';
    }
    const user = normalizeUser(jid);
    return user ? `${user}@lid` : '';
};

export const createLidResolver = ({ ev, getOwnJid, logger } = {}) => {
    const lidToPn = new Map();
    const pnToLid = new Map();

    const learn = (lid, pn) => {
        const lidJid = asLidJid(lid);
        const pnJid = asPnJid(pn);
        if (!lidJid || !pnJid || lidJid === pnJid) {
            return;
        }
        lidToPn.set(lidJid, pnJid);
        pnToLid.set(pnJid, lidJid);
    };

    const learnOwnJid = () => {
        const own = String(getOwnJid?.() || '');
        const pnJid = asPnJid(own);
        const lidJid = asLidJid(own);
        if (pnJid && lidJid) {
            learn(lidJid, pnJid);
        }
        else if (pnJid) {
            pnToLid.set(pnJid, pnJid);
        }
        else if (lidJid) {
            lidToPn.set(lidJid, lidJid);
        }
    };

    const observeContacts = (contacts) => {
        for (const contact of contacts || []) {
            if (!contact || typeof contact !== 'object') {
                continue;
            }
            const id = contact.id || contact.jid;
            learn(contact.lid || id, contact.pn || contact.phoneNumber || id);
        }
    };

    const observeChats = (chats) => {
        for (const chat of chats || []) {
            if (!chat || typeof chat !== 'object') {
                continue;
            }
            const id = chat.id || chat.jid;
            learn(chat.lid || chat.remoteJidAlt || id, chat.pn || chat.phoneNumber || id);
        }
    };

    const observeMessages = (payload) => {
        const messages = Array.isArray(payload) ? payload : payload?.messages;
        for (const message of messages || []) {
            const key = message?.key || {};
            const senderPn = message?.senderPn || key.senderPn;
            const participantPn = message?.participantPn || key.participantPn;
            learn(key.remoteJidAlt || key.remoteJid, senderPn || participantPn || key.remoteJid);
            learn(key.participantAlt || key.participant, participantPn || senderPn || key.participant);
            learn(key.remoteJidAlt || key.participantAlt, senderPn || participantPn);
        }
    };

    const observeHistory = (payload) => {
        observeContacts(payload?.contacts);
        observeChats(payload?.chats);
        observeMessages(payload);
        for (const mapping of payload?.lidPnMappings || []) {
            learn(mapping?.lid, mapping?.pn);
        }
    };

    const observeGroup = (payload) => {
        const groups = Array.isArray(payload) ? payload : [payload];
        for (const group of groups) {
            if (!group || typeof group !== 'object') {
                continue;
            }
            learn(group.lid || group.id, group.pn || group.id);
            for (const participant of group.participants || []) {
                learn(participant?.lid || participant?.id, participant?.pn || participant?.id);
            }
        }
    };

    const toPn = (jid) => {
        if (typeof jid !== 'string' || !jid) {
            return jid;
        }
        const lidJid = asLidJid(jid);
        if (!lidJid) {
            return jid;
        }
        const pnJid = lidToPn.get(lidJid);
        if (!pnJid) {
            learnOwnJid();
            const ownPn = lidToPn.get(lidJid);
            return ownPn || jid;
        }
        const device = String(jid).split('@')[0].split(':')[1];
        return device ? `${pnJid.split('@')[0]}:${device}@s.whatsapp.net` : pnJid;
    };

    const toLid = (jid) => {
        const pnJid = asPnJid(jid);
        if (!pnJid) {
            return asLidJid(jid) || jid;
        }
        return pnToLid.get(pnJid) || jid;
    };

    const resolveJid = (jid) => {
        if (asLidJid(jid)) {
            return toPn(jid);
        }
        if (asPnJid(jid)) {
            return toLid(jid);
        }
        return jid;
    };

    const resolve = toPn;

    const attach = () => {
        learnOwnJid();
        if (!ev?.on) {
            return;
        }
        ev.on('contacts.upsert', observeContacts);
        ev.on('contacts.update', observeContacts);
        ev.on('chats.upsert', observeChats);
        ev.on('messaging-history.set', observeHistory);
        ev.on('groups.upsert', observeGroup);
        ev.on('groups.update', observeGroup);
        ev.on('group-participants.update', observeGroup);
        ev.on('messages.upsert', observeMessages);
    };

    return {
        resolve,
        resolveJid,
        toPn,
        toLid,
        learn,
        observeContacts,
        observeChats,
        observeMessages,
        observeHistory,
        observeGroup,
        attach,
        lidToPn,
        pnToLid,
        get size() {
            return lidToPn.size;
        },
        entries: () => [...lidToPn.entries()]
    };
};
