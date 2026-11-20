import { DEFAULT_CONNECTION_CONFIG } from '../Defaults/index.js';
import { createLidResolver } from '../Utils/lid-resolver.js';
import { createMessageStore } from '../Utils/message-store.js';
import { configurePollContext } from '../Utils/messages.js';
import { makeCommunitiesSocket } from './communities.js';
// export the last socket layer
const makeWASocket = (config = {}) => {
    const messageStore = config.messageStore || createMessageStore(config.messageStoreLimit || 500, () => config.auth?.creds?.me?.id);
    const newConfig = {
        ...DEFAULT_CONNECTION_CONFIG,
        ...config,
        messageStore,
        getMessage: config.getMessage || messageStore.get
    };
    const sock = makeCommunitiesSocket(newConfig);
    const persistDir = config.lidMappingDir || config.authDir || config.sessionDir || config.auth?.creds?.dir;
    const lidResolver = config.lidResolver || createLidResolver({
        ev: sock.ev,
        getOwnJid: () => sock.user?.id || config.auth?.creds?.me?.id,
        logger: newConfig.logger,
        persistPath: config.lidMappingPath,
        persistDir,
        debounceMs: config.lidMappingDebounceMs ?? 500
    });
    lidResolver.attach();
    configurePollContext({
        getMessage: newConfig.getMessage,
        getOwnJid: () => sock.user?.id || config.auth?.creds?.me?.id
    });
    sock.messageStore = messageStore;
    sock.lidResolver = lidResolver;
    sock.lidToPn = lidResolver.lidToPn || sock.lidToPn;
    sock.pnToLid = lidResolver.pnToLid || sock.pnToLid;
    sock.resolveJid = (jid) => typeof lidResolver.resolveJid === 'function'
        ? lidResolver.resolveJid(jid)
        : (typeof lidResolver.resolve === 'function' ? lidResolver.resolve(jid) : jid);
    sock.getPn = (lid) => typeof lidResolver.getPn === 'function'
        ? lidResolver.getPn(lid)
        : lidResolver.toPn?.(lid) || lid;
    sock.getLid = (pn) => typeof lidResolver.getLid === 'function'
        ? lidResolver.getLid(pn)
        : lidResolver.toLid?.(pn) || pn;
    sock.flushLidMapping = () => typeof lidResolver.flush === 'function' ? lidResolver.flush() : false;
    return sock;
};
export default makeWASocket;
//# sourceMappingURL=index.js.map
