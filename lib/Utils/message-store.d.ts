import type { WAMessage, WAMessageKey } from '../Types/index.js';
export declare const DEFAULT_MESSAGE_STORE_LIMIT = 500;
export declare const POLL_CONTEXT: unique symbol;
export type MessageStore = {
    put: (msg: WAMessage) => WAMessage;
    get: (key: WAMessageKey | string) => WAMessage | undefined;
    has: (key: WAMessageKey | string) => boolean;
    clear: () => void;
    readonly size: number;
};
export declare const createMessageStore: (limit?: number, getOwnJid?: () => string | undefined) => MessageStore;
