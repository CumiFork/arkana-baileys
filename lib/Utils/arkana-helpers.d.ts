import type { MiscMessageGenerationOptions, WAMessage } from '../Types/index.js';

export type ArkanaMediaInput = Buffer | Uint8Array | string;

export type ArkanaFlowButton = string | {
    name?: string;
    type?: string;
    label?: string;
    text?: string;
    displayText?: string;
    id?: string;
    buttonParamsJson?: string;
    [key: string]: unknown;
};

export type ArkanaCarouselCard = {
    title?: string;
    body?: string;
    text?: string;
    footer?: string;
    caption?: string;
    image?: ArkanaMediaInput;
    video?: ArkanaMediaInput;
    buttons?: ArkanaFlowButton[];
    messageVersion?: number;
    messageParamsJson?: string;
};

export type ArkanaCarouselOptions = {
    text?: string;
    title?: string;
    footer?: string;
    cards: ArkanaCarouselCard[];
    contextInfo?: Record<string, unknown>;
};

export type ArkanaAlbumItem = {
    image?: ArkanaMediaInput;
    video?: ArkanaMediaInput;
    caption?: string;
    mimetype?: string;
    fileName?: string;
};

export type ArkanaPollOptions = {
    name: string;
    values: (string | { optionName: string })[];
    selectableCount?: number;
    [key: string]: unknown;
};

export type ArkanaEventOptions = {
    name: string;
    description?: string;
    startTime: number | string | Date;
    endTime?: number | string | Date;
    location?: Record<string, unknown>;
    joinLink?: string;
    [key: string]: unknown;
};

export type ArkanaPaymentOptions = {
    amount?: number;
    amount1000?: number;
    currency?: string;
    note?: string;
    expiry?: number | string | Date;
    background?: Uint8Array | string;
    [key: string]: unknown;
};

export type ArkanaOrderOptions = {
    orderId?: string;
    itemCount?: number;
    thumbnail?: Uint8Array | string;
    status?: string | number;
    surface?: string | number;
    message?: string;
    orderTitle?: string;
    sellerJid?: string;
    token?: string;
    totalAmount1000?: number;
    totalCurrencyCode?: string;
    product?: Record<string, unknown>;
    contextInfo?: Record<string, unknown>;
    [key: string]: unknown;
};

export type ArkanaAdOptions = {
    title?: string;
    body?: string;
    thumbnail?: Uint8Array | string;
    thumbnailUrl?: string;
    mediaUrl?: string;
    sourceUrl?: string;
    mediaType?: number | 'image' | 'video';
    renderLargerThumbnail?: boolean;
    contextInfo?: Record<string, unknown>;
};

export declare const ARKANA_MEDIA_HOLDERS: Readonly<Record<string, string>>;
export declare const stripArkanaDevice: (jid: string) => string;
export declare const unwrapArkanaMessage: <T>(message: T) => T;
export declare const buildAdReplyContext: (adOptions?: ArkanaAdOptions) => Record<string, unknown>;
export declare const buildCarouselContent: (options: ArkanaCarouselOptions, prepareMedia: (content: Record<string, unknown>, options?: Record<string, unknown>) => Promise<Record<string, any>>) => Promise<Record<string, unknown>>;
export declare const buildAlbumContent: (medias: ArkanaAlbumItem[], prepareMedia: (content: Record<string, unknown>, options?: Record<string, unknown>) => Promise<Record<string, any>>) => Promise<{ albumMessage: Record<string, unknown>; children: Record<string, any>[] }>;
export declare const buildPollV3Content: (options: ArkanaPollOptions) => { pollCreationMessageV3: Record<string, unknown> };
export declare const buildEventContent: (options: ArkanaEventOptions) => { eventMessage: Record<string, unknown> };
export declare const buildPaymentRequestContent: (options: ArkanaPaymentOptions) => { requestPaymentMessage: Record<string, unknown> };
export declare const buildOrderContent: (options: ArkanaOrderOptions) => { orderMessage: Record<string, unknown> } | { productMessage: Record<string, unknown> };
export declare const buildAdReplyContent: (text: string | Record<string, unknown>, adOptions?: ArkanaAdOptions) => Record<string, unknown>;

export type ArkanaHelpers = {
    sendCarousel: (jid: string, options: ArkanaCarouselOptions, sendOptions?: MiscMessageGenerationOptions) => Promise<WAMessage | undefined>;
    sendAlbum: (jid: string, medias: ArkanaAlbumItem[], options?: MiscMessageGenerationOptions) => Promise<WAMessage & { childIds: string[] }>;
    sendPollV3: (jid: string, options: ArkanaPollOptions, sendOptions?: MiscMessageGenerationOptions) => Promise<WAMessage | undefined>;
    sendEvent: (jid: string, options: ArkanaEventOptions, sendOptions?: MiscMessageGenerationOptions) => Promise<WAMessage | undefined>;
    sendPaymentRequest: (jid: string, options: ArkanaPaymentOptions, sendOptions?: MiscMessageGenerationOptions) => Promise<WAMessage | undefined>;
    sendOrder: (jid: string, options: ArkanaOrderOptions, sendOptions?: MiscMessageGenerationOptions) => Promise<WAMessage | undefined>;
    sendViewOnce: (jid: string, mediaContent: Record<string, unknown>, sendOptions?: MiscMessageGenerationOptions) => Promise<WAMessage | undefined>;
    sendAdReply: (jid: string, text: string | Record<string, unknown>, adOptions?: ArkanaAdOptions, sendOptions?: MiscMessageGenerationOptions) => Promise<WAMessage | undefined>;
    resolveStatusTargets: (jids: string[]) => string[];
    buildCarouselContent: (options: ArkanaCarouselOptions) => Promise<Record<string, unknown>>;
    buildAlbumContent: (medias: ArkanaAlbumItem[]) => Promise<{ albumMessage: Record<string, unknown>; children: Record<string, any>[] }>;
    buildPollV3Content: typeof buildPollV3Content;
    buildEventContent: typeof buildEventContent;
    buildPaymentRequestContent: typeof buildPaymentRequestContent;
    buildOrderContent: typeof buildOrderContent;
    buildAdReplyContent: typeof buildAdReplyContent;
};

export declare const createArkanaHelpers: (deps?: {
    sendMessage?: (...args: any[]) => Promise<WAMessage | undefined>;
    relayMessage?: (...args: any[]) => Promise<string>;
    prepareMedia?: (content: Record<string, unknown>, options?: Record<string, unknown>) => Promise<Record<string, any>>;
    resolveJid?: (jid: string) => string;
    getPn?: (jid: string) => string;
    userJid?: string;
}) => ArkanaHelpers;

export default createArkanaHelpers;
