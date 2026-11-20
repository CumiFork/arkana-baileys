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
    const lidResolver = config.lidResolver || createLidResolver({
        ev: sock.ev,
        getOwnJid: () => sock.user?.id || config.auth?.creds?.me?.id,
        logger: newConfig.logger
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
    return sock;
};
export default makeWASocket;
//# sourceMappingURL=index.js.map
