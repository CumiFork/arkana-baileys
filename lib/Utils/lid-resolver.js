import fs from 'node:fs';
import path from 'node:path';

const DEFAULT_PERSIST_FILE = 'lid-mapping.json';

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

const withDevice = (jid, device) => {
    if (!device) {
        return jid;
    }
    const base = String(jid).split('@')[0].split(':')[0];
    const domain = String(jid).split('@')[1];
    return domain ? `${base}:${device}@${domain}` : jid;
};

export const createLidResolver = ({
    ev,
    getOwnJid,
    logger,
    persistPath,
    persistDir,
    debounceMs = 500
} = {}) => {
    const lidToPn = new Map();
    const pnToLid = new Map();
    let loading = true;
    let dirty = false;
    let persistTimer = null;

    const resolvePersistPath = () => {
        if (typeof persistPath === 'string' && persistPath) {
            return persistPath;
        }
        const dir = persistDir || process.env.LID_MAPPING_DIR;
        return dir ? path.join(dir, DEFAULT_PERSIST_FILE) : null;
    };

    const flush = () => {
        if (persistTimer) {
            clearTimeout(persistTimer);
            persistTimer = null;
        }
        if (!dirty) {
            return false;
        }
        const file = resolvePersistPath();
        if (!file) {
            dirty = false;
            return false;
        }
        try {
            fs.mkdirSync(path.dirname(file), { recursive: true });
            const payload = {
                version: 1,
                updatedAt: Date.now(),
                lidToPn: [...lidToPn.entries()].filter(([lid, pn]) => lid !== pn),
                pnToLid: [...pnToLid.entries()].filter(([pn, lid]) => lid !== pn)
            };
            const tempFile = `${file}.tmp`;
            fs.writeFileSync(tempFile, JSON.stringify(payload));
            fs.renameSync(tempFile, file);
            dirty = false;
            return true;
        } catch (error) {
            logger?.warn?.({ error }, 'failed to persist lid mapping');
            return false;
        }
    };

    const schedulePersist = () => {
        if (loading || !resolvePersistPath()) {
            return;
        }
        dirty = true;
        if (persistTimer) {
            clearTimeout(persistTimer);
        }
        persistTimer = setTimeout(flush, Math.max(1, Number(debounceMs) || 500));
        persistTimer.unref?.();
    };

    const learn = (lid, pn) => {
        const lidJid = asLidJid(lid);
        const pnJid = asPnJid(pn);
        if (!lidJid || !pnJid || lidJid === pnJid) {
            return false;
        }
        if (lidToPn.get(lidJid) === pnJid && pnToLid.get(pnJid) === lidJid) {
            return false;
        }
        lidToPn.set(lidJid, pnJid);
        pnToLid.set(pnJid, lidJid);
        schedulePersist();
        return true;
    };

    const load = () => {
        const file = resolvePersistPath();
        if (!file || !fs.existsSync(file)) {
            return 0;
        }
        loading = true;
        let loaded = 0;
        try {
            const payload = JSON.parse(fs.readFileSync(file, 'utf8'));
            for (const [lid, pn] of payload?.lidToPn || []) {
                if (learn(lid, pn)) {
                    loaded += 1;
                }
            }
            for (const [pn, lid] of payload?.pnToLid || []) {
                if (learn(lid, pn)) {
                    loaded += 1;
                }
            }
        } catch (error) {
            logger?.warn?.({ error, file }, 'failed to load lid mapping');
        } finally {
            loading = false;
            dirty = false;
        }
        return loaded;
    };

    const learnOwnJid = () => {
        const own = String(getOwnJid?.() || '');
        if (!own) {
            return;
        }
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
            return lidToPn.get(lidJid) || jid;
        }
        const device = String(jid).split('@')[0].split(':')[1];
        return withDevice(pnJid, device);
    };

    const toLid = (jid) => {
        if (typeof jid !== 'string' || !jid) {
            return jid;
        }
        const pnJid = asPnJid(jid);
        if (!pnJid) {
            return asLidJid(jid) || jid;
        }
        const lidJid = pnToLid.get(pnJid);
        if (!lidJid || lidJid === pnJid) {
            return jid;
        }
        const device = String(jid).split('@')[0].split(':')[1];
        return withDevice(lidJid, device);
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

    const getPn = (lid) => toPn(lid);
    const getLid = (pn) => toLid(pn);
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

    load();
    loading = false;

    return {
        resolve,
        resolveJid,
        toPn,
        toLid,
        getPn,
        getLid,
        learn,
        load,
        flush,
        persistPath: resolvePersistPath(),
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
