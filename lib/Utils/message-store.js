export const DEFAULT_MESSAGE_STORE_LIMIT = 500;
export const POLL_CONTEXT = Symbol.for('arkana.pollContext');

const keyFor = (key = {}) => [key.remoteJid || '', key.participant || '', key.id || ''].join('|');

export const createMessageStore = (limit = DEFAULT_MESSAGE_STORE_LIMIT, getOwnJid) => {
    const max = Number.isFinite(limit) && limit > 0 ? limit : DEFAULT_MESSAGE_STORE_LIMIT;
    const byKey = new Map();
    const byId = new Map();

    const put = (msg) => {
        const key = msg?.key;
        if (!key?.id) {
            return msg;
        }

        const keyId = keyFor(key);
        const previous = byKey.get(keyId);
        if (previous && previous !== msg) {
            const previousId = previous.key?.id;
            if (previousId && byId.get(previousId) === previous) {
                byId.delete(previousId);
            }
        }

        byKey.set(keyId, msg);
        byId.set(key.id, msg);
        if (byKey.size > max) {
            const oldestKey = byKey.keys().next().value;
            const oldest = byKey.get(oldestKey);
            byKey.delete(oldestKey);
            if (oldest?.key?.id && byId.get(oldest.key.id) === oldest) {
                byId.delete(oldest.key.id);
            }
        }

        if (!Object.prototype.hasOwnProperty.call(msg, POLL_CONTEXT)) {
            Object.defineProperty(msg, POLL_CONTEXT, {
                value: { getMessage: get, getOwnJid },
                enumerable: false,
                configurable: true
            });
        }
        return msg;
    };

    const get = (key) => {
        if (!key) {
            return undefined;
        }
        if (typeof key === 'string') {
            return byId.get(key);
        }
        const exact = byKey.get(keyFor(key));
        if (exact) {
            return exact;
        }
        const id = key.id || key.stanzaId;
        return id ? byId.get(id) : undefined;
    };

    const has = (key) => Boolean(get(key));

    const clear = () => {
        byKey.clear();
        byId.clear();
    };

    return {
        put,
        get,
        has,
        clear,
        get size() {
            return byKey.size;
        }
    };
};
