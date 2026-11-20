import type { EventEmitter } from 'events';
export type LidResolver = {
    resolve: (jid: string) => string;
    learn: (lid: unknown, pn: unknown) => void;
    observeContacts: (contacts: unknown[] | undefined) => void;
    observeChats: (chats: unknown[] | undefined) => void;
    observeMessages: (payload: unknown) => void;
    attach: () => void;
    readonly size: number;
    entries: () => [string, string][];
};
export declare const createLidResolver: (deps?: {
    ev?: EventEmitter;
    getOwnJid?: () => string | undefined;
    logger?: unknown;
}) => LidResolver;
