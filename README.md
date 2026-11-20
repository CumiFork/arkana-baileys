<div align="center">
  <img src="./assets/banner.svg" alt="ARKANA-BAILEYS v1.3.0-mod" width="1200" style="max-width: 100%; height: auto; aspect-ratio: 1200 / 320;" />
</div>

<div align="center">

[![Node.js](https://img.shields.io/badge/Node.js-v24%2B-339933?style=for-the-badge&logo=node.js&logoColor=white)](https://nodejs.org/)
[![release](https://img.shields.io/badge/release-v1.3.0--mod-10B981?style=for-the-badge)](https://github.com/CumiFork/arkana-baileys/releases)
[![last commit](https://img.shields.io/github/last-commit/CumiFork/arkana-baileys?style=for-the-badge&color=38BDF8&logo=github)](https://github.com/CumiFork/arkana-baileys/commits/main)
[![repo size](https://img.shields.io/github/repo-size/CumiFork/arkana-baileys?style=for-the-badge&color=F43F5E&logo=github)](https://github.com/CumiFork/arkana-baileys)

</div>

`arkana-baileys` adalah fork Baileys untuk Node.js v24+ Pure ESM. Export bawaan Baileys tetap tersedia. Mod yang ditambahkan mencakup injeksi stanza `<biz>`, sanitizer JID, resolver `@lid` persisten, helper native flow, dan skema `WAProto` dengan field `addonActionSection = 115`.

## Ringkasan modifikasi core

| Perubahan | Lokasi | Dampak |
| --- | --- | --- |
| Injeksi `<biz>` otomatis | `lib/Socket/messages-send.js` | `relayMessage` menambahkan node `<interactive>` untuk `interactiveMessage`, `interactiveResponseMessage`, `buttonsMessage`, dan `templateMessage`. Node `<biz>` yang sudah ada tidak diduplikasi, dan `additionalNodes` bawaan seperti `<bot biz_bot="1"/>` tetap dipertahankan. |
| Nama flow dinamis | `lib/Socket/messages-send.js` | Atribut `<native_flow v="2" name="..."/>` diambil dari `nativeFlowMessage.buttons[0].name`. Fallback `galaxy_message` dipakai untuk response, dan `mpm` dipakai jika tombol tidak tersedia. |
| Sanitizer suffix perangkat | `lib/Socket/messages-send.js`, `lib/Socket/socket.js` | Suffix seperti `:12@` dibuang dari target dan `userJid`. Domain `@s.whatsapp.net`, `@g.us`, `@newsletter`, `@lid`, dan `@broadcast` tidak berubah. |
| Resolver `@lid` persisten | `lib/Utils/lid-resolver.js` | Pasangan `@lid` dan `@s.whatsapp.net` dipelajari dari contacts, chats, grup, riwayat, dan pesan masuk. Cache ditulis ke `lid-mapping.json` dengan debounced write, lalu dimuat ulang saat socket dibuat. |
| Signal JID | `lib/Socket/messages-send.js` | `relayMessage` memakai JID LID saat pasangan tersedia, sehingga `assertSessions` dan enkripsi memakai sesi yang sama. `msg.senderPn` dan `msg.senderLid` tidak memicu `No Session` hanya karena resolusi JID. |
| Skema proto tambahan | `WAProto/WAProto.proto`, `WAProto/index.proto` | `AddonActionSection` ditambahkan pada field `115` di `Message`, dengan `AddonAction` dan `Primitive` bersarang. Binding ESM dan `.d.ts` sudah diregenerasi. |
| Deklarasi TypeScript | `lib/**/*.d.ts` | Seluruh method mod, helper baru, dan properti pesan memiliki signature. |

## Daftar fitur

### Fitur bawaan yang dipertahankan

- Semua export Baileys, termasuk `makeWASocket`, `useMultiFileAuthState`, `fetchLatestBaileysVersion`, `DisconnectReason`, `proto`, dan utilitas pesan.
- Pairing QR dan pairing code kustom delapan karakter.
- Pesan teks, media, sticker, video note, dan view once.
- Poll `pollCreationMessageV3`, event, album, status mention, hidetag, dan payment request.
- Auto-mention untuk nomor berformat `@628...` pada teks, caption, dan teks kartu.
- Forwarded badge, `channelBadge`, `fakeQuote`, dan `extractMessage`.
- LRU poll decrypt, status mention, dan shortcut flow dari rilis `1.3.0-mod`.

### Helper mod pada instance `sock`

| Method | Fungsi |
| --- | --- |
| `sendInteractive(jid, opts, quoted)` | Membangun pesan interaktif dari `text`, `title`, `footer`, media, dan `buttons`. |
| `sendGalaxy(jid, opts)` | Mengirim `interactiveResponseMessage` lewat `relayMessage`. |
| `sendRichMessage(jid, opts, quoted)` | Mengirim blok teks, kode, tabel, gambar, kartu, dan tombol dengan fallback Markdown. |
| `resolveJid(jid)` | Mengubah arah JID berdasarkan tabel resolver. |
| `getLid(pn)` dan `getPn(lid)` | Konversi eksplisit PN ke LID dan LID ke PN. |
| `signalJid(jid)` | Memilih JID yang cocok untuk sesi Signal aktif. |
| `flushLidMapping()` | Menulis cache resolver ke disk segera. |
| `sendCarousel(jid, opts, options)` | Menyiapkan media header tiap kartu, menyusun `carouselMessage`, dan memicu injeksi `<biz>`. |
| `sendAlbum(jid, medias, options)` | Mengirim `albumMessage` dengan `associatedChildMessage` per media. |
| `sendStatusMention(jids, content, options)` | Mengirim ke `status@broadcast` dengan `statusJidList` dari `@lid` yang sudah diubah ke PN. Signature lama `(content, jids, options)` tetap diterima. |
| `sendPollV3(jid, opts, options)` | Wrapper `pollCreationMessageV3`. |
| `sendEvent(jid, opts, options)` | Wrapper `eventMessage`. |
| `sendPaymentRequest(jid, opts, options)` | Wrapper `requestPaymentMessage`, `amount * 1000` dihitung otomatis. |
| `sendOrder(jid, orderData, options)` | Wrapper `orderMessage` atau `productMessage`. |
| `downloadMedia(msg, type)` | Mengunduh media dari pesan atau `quoted`, termasuk wrapper view once dan ephemeral. |
| `sendViewOnce(jid, mediaContent, options)` | Shortcut media sekali lihat. |
| `sendAdReply(jid, text, adOptions, options)` | Shortcut pesan dengan `contextInfo.externalAdReply`. |

Builder payload seperti `buildCarouselContent`, `buildAlbumContent`, `buildPollV3Content`, `buildEventContent`, `buildPaymentRequestContent`, `buildOrderContent`, dan `buildAdReplyContent` juga diekspor untuk pengujian tanpa koneksi.

## Instalasi

`arkana-baileys` mensyaratkan Node.js v24+ dan modul ESM. Tambahkan dependensi dari GitHub:

```json
{
  "type": "module",
  "dependencies": {
    "arkana-baileys": "github:CumiFork/arkana-baileys"
  }
}
```

Atau pasang langsung:

```bash
npm install github:CumiFork/arkana-baileys
```

### Catatan Termux Android

Di `/storage/emulated/0`, symlink `node_modules/.bin` tidak didukung. Gunakan:

```bash
npm install --no-bin-links --no-audit --no-fund
```

Jika project memakai `patch-package`, jalankan langsung dengan `node ./node_modules/patch-package/index.js` agar tidak bergantung pada symlink `.bin`.

## Inisialisasi socket

```js
import makeWASocket, {
  DisconnectReason,
  fetchLatestBaileysVersion,
  useMultiFileAuthState
} from 'arkana-baileys'

const { state, saveCreds } = await useMultiFileAuthState('./auth')
const { version } = await fetchLatestBaileysVersion()

const sock = makeWASocket({
  version,
  auth: state,
  lidMappingDir: './auth'
})

sock.ev.on('creds.update', saveCreds)
sock.ev.on('connection.update', ({ connection, lastDisconnect, qr }) => {
  if (qr) console.log('Scan QR ini:', qr)
  if (connection === 'close') {
    const loggedOut = lastDisconnect?.error?.output?.statusCode === DisconnectReason.loggedOut
    if (!loggedOut) process.exit(1)
  }
})
```

`lidMappingDir` menentukan folder penyimpanan `lid-mapping.json`. Gunakan `lidMappingPath` jika ingin nama file lain, dan `lidMappingDebounceMs` untuk mengatur jeda tulis. Tanpa keduanya, resolver tetap bekerja di memori.

## Quick usage

### `sendInteractive` dan `sendGalaxy`

```js
const jid = '6281234567890@s.whatsapp.net'

await sock.sendInteractive(jid, {
  text: 'Pilih layanan:',
  title: 'Layanan Arkana',
  footer: 'Dikirim oleh arkana-baileys',
  buttons: [
    { type: 'quick_reply', display_text: 'Harga', id: 'harga' },
    { type: 'cta_url', display_text: 'Website', url: 'https://github.com/CumiFork/arkana-baileys' }
  ]
})

await sock.sendGalaxy(jid, {
  text: 'Selesai',
  name: 'galaxy_message',
  paramsJson: { status: 'success', screen: 'result' },
  version: 1
})
```

### `sendCarousel`

```js
import { readFileSync } from 'node:fs'

await sock.sendCarousel(jid, {
  text: 'Katalog hari ini',
  footer: 'Geser untuk melihat kartu',
  title: 'Produk',
  cards: [
    {
      title: 'Kartu Pertama',
      body: 'Deskripsi produk pertama.',
      image: readFileSync('./produk-1.jpg'),
      buttons: [{ label: 'Beli', id: 'beli_1' }]
    },
    {
      title: 'Kartu Kedua',
      body: 'Deskripsi produk kedua.',
      video: readFileSync('./produk-2.mp4'),
      buttons: [{ label: 'Detail', id: 'detail_2' }]
    }
  ]
})
```

### `sendAlbum`

```js
await sock.sendAlbum(jid, [
  { image: readFileSync('./1.jpg'), caption: 'Gambar pertama' },
  { video: readFileSync('./2.mp4'), caption: 'Video kedua' }
])
```

### `sendStatusMention`

```js
await sock.sendStatusMention([
  '6281234567890@s.whatsapp.net',
  '120363012345678901@g.us'
], { text: 'Halo semua!' }, {
  backgroundColor: '#FF5500',
  font: 2
})
```

### `sendPollV3` dan `sendEvent`

```js
await sock.sendPollV3(jid, {
  name: 'Pilih jadwal',
  values: ['Sabtu', 'Minggu'],
  selectableCount: 1
})

await sock.sendEvent(jid, {
  name: 'Rapat Arkana',
  description: 'Diskusi rilis 1.3.0',
  startTime: Math.floor(Date.now() / 1000) + 3600,
  endTime: Math.floor(Date.now() / 1000) + 7200,
  location: { degreesLatitude: -6.2, degreesLongitude: 106.8 },
  joinLink: 'https://example.test/rapat'
})
```

### `downloadMedia`

```js
sock.ev.on('messages.upsert', async ({ messages }) => {
  for (const msg of messages) {
    const target = msg.quotedMessage || msg
    try {
      const buffer = await sock.downloadMedia(target)
      console.log('Media diterima:', buffer.length, 'byte')
    } catch (error) {
      if (!/does not contain media/.test(error.message)) throw error
    }
  }
})
```

`downloadMedia` menerima `buffer`, `stream`, `base64`, dan `data-url` sebagai argumen kedua. Wrapper yang dibuka meliputi `viewOnceMessage`, `viewOnceMessageV2`, `viewOnceMessageV2Extension`, `ephemeralMessage`, dan `documentWithCaptionMessage`.

### `resolveJid`, `getLid`, `getPn`, dan `msg.senderPn`

```js
const pn = sock.resolveJid('123456@lid')
const lid = sock.resolveJid('6281234567890@s.whatsapp.net')

console.log(pn, lid)
console.log(sock.getLid('6281234567890@s.whatsapp.net'))
console.log(sock.getPn('123456@lid'))

sock.ev.on('messages.upsert', ({ messages }) => {
  for (const msg of messages) {
    console.log('Pengirim PN:', msg.senderPn)
    console.log('Pengirim LID:', msg.senderLid)
  }
})
```

### Payment, order, view once, dan ad reply

```js
await sock.sendPaymentRequest(jid, {
  amount: 50000,
  currency: 'IDR',
  note: 'Tagihan langganan'
})

await sock.sendOrder(jid, {
  orderId: 'ORDER-42',
  itemCount: 2,
  totalAmount1000: 125000,
  totalCurrencyCode: 'IDR'
})

await sock.sendViewOnce(jid, { image: readFileSync('./rahasia.jpg') })

await sock.sendAdReply(jid, 'Promo hari ini', {
  title: 'Judul kartu',
  body: 'Isi kartu',
  thumbnailUrl: 'https://example.test/thumb.png',
  sourceUrl: 'https://example.test',
  mediaType: 'image',
  renderLargerThumbnail: true
})
```

Contoh lengkap lain ada di [`examples/mod-showcase.js`](./examples/mod-showcase.js).

## Kompatibilitas dan verifikasi

- Pure ESM. Tidak ada `require()` di `lib/` maupun `WAProto/index.js`.
- `node --check` dijalankan pada file JS yang diubah.
- `import('arkana-baileys')` diverifikasi di Node.js v24.22.3.
- Encode dan decode `proto.Message` dengan `addonActionSection` tag `115` diverifikasi.
- Penggabungan `additionalNodes`, nama flow dinamis, persistensi resolver, dan builder payload diuji tanpa koneksi WhatsApp.
- `sock.sendInteractive()` dan `sock.sendGalaxy()` tetap menerima signature lama.

## Struktur direktori

```text
arkana-baileys/
├── assets/
│   └── banner.svg
├── examples/
│   └── mod-showcase.js
├── lib/
│   ├── Defaults/
│   ├── Signal/
│   ├── Socket/
│   │   ├── index.js
│   │   ├── messages-send.js
│   │   ├── messages-recv.js
│   │   └── socket.js
│   ├── Utils/
│   │   ├── arkana-helpers.js
│   │   ├── lid-resolver.js
│   │   └── messages.js
│   ├── Types/
│   └── index.js
├── WAProto/
│   ├── WAProto.proto
│   ├── index.proto
│   ├── index.js
│   └── index.d.ts
├── CHANGELOG.md
├── LICENSE
├── README.md
└── package.json
```

## Catatan

Skema kanonik ada di `WAProto/index.proto` dan disalin identik ke `WAProto/WAProto.proto`. Field `addonActionSection = 115` sengaja ditambahkan tanpa mengubah field lain.

Proyek ini bukan produk resmi WhatsApp. Gunakan sesuai kebijakan layanan WhatsApp dan tanggung jawab Anda sendiri. Kode ini dilanjutkan dari Baileys oleh WhiskeySockets di bawah lisensi MIT.

## Kredit

Dikelola oleh [CumiFork](https://github.com/CumiFork). Fork dari [WhiskeySockets/Baileys](https://github.com/WhiskeySockets/Baileys) dengan perubahan untuk native flow, resolver LID persisten, helper pengiriman, dan runtime Node.js v24 di Termux.
