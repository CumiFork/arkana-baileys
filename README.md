# arkana-baileys

Fork kustom Baileys untuk Node.js v24 di Termux. Export bawaan Baileys tetap tersedia, dengan tambahan shortcut pesan interaktif, carousel, sticker view once, dan pairing code kustom.

## Instalasi

```bash
npm install github:CumiFork/arkana-baileys
```

Import bawaan tetap memakai default export:

```js
import makeWASocket, { useMultiFileAuthState, fetchLatestBaileysVersion, DisconnectReason } from 'arkana-baileys'
```

## Tombol interaktif

`interactiveButtons` menghasilkan `interactiveMessage` dengan `nativeFlowMessage`. Tiap tombol memakai `name` dan `buttonParamsJson`, baik sebagai objek maupun string JSON.

```js
const { state, saveCreds } = await useMultiFileAuthState('auth')

const sock = makeWASocket({
  version: (await fetchLatestBaileysVersion()).version,
  auth: state
})

sock.ev.on('creds.update', saveCreds)
sock.ev.on('connection.update', ({ connection, qr }) => {
  if (connection === 'close') return
  if (connection !== 'open' && qr) console.log(qr)
})

await sock.sendMessage('6281234567890@s.whatsapp.net', {
  text: 'Pilih layanan:',
  title: 'Layanan Arkana',
  subtitle: 'Support 24 jam',
  footer: 'Dikirim oleh arkana-baileys',
  interactiveButtons: [
    {
      name: 'quick_reply',
      buttonParamsJson: JSON.stringify({ display_text: 'Harga', id: 'harga' })
    },
    {
      name: 'cta_url',
      buttonParamsJson: {
        display_text: 'Website',
        url: 'https://github.com/CumiFork/arkana-baileys'
      }
    }
  ]
})
```

Node `biz` untuk native flow disuntikkan otomatis oleh `relayMessage`, termasuk saat pesan dibungkus `viewOnceMessageV2`, `ephemeralMessage`, atau wrapper lainnya.

## Carousel kartu

`cards` menghasilkan `interactiveMessage` dengan `carouselMessage`. Setiap kartu boleh memiliki header media, judul, subtitle, body, footer, dan tombol.

```js
await sock.sendMessage('6281234567890@s.whatsapp.net', {
  text: 'Katalog hari ini',
  footer: 'Geser untuk melihat kartu',
  cards: [
    {
      title: 'Kartu Pertama',
      subtitle: 'Stok tersedia',
      text: 'Deskripsi singkat produk pertama.',
      image: { url: 'https://example.com/produk-1.jpg' },
      buttons: [
        {
          name: 'quick_reply',
          buttonParamsJson: { display_text: 'Beli', id: 'beli_1' }
        }
      ]
    },
    {
      title: 'Kartu Kedua',
      text: 'Deskripsi singkat produk kedua.',
      image: { url: 'https://example.com/produk-2.jpg' },
      buttons: [
        {
          name: 'cta_url',
          buttonParamsJson: { display_text: 'Detail', url: 'https://example.com/produk-2' }
        }
      ]
    }
  ]
})
```

`image`, `video`, dan `document` pada kartu diproses lewat upload media yang sama dengan pesan media Baileys biasa.

## Sticker view once

```js
import { readFileSync } from 'fs'

await sock.sendMessage('6281234567890@s.whatsapp.net', {
  sticker: readFileSync('./sticker.webp'),
  viewOnce: true
})
```

Sticker dibungkus ke dalam `viewOnceMessageV2`, dengan `stickerMessage.viewOnce = true`.

## Pairing code kustom

Kode kustom wajib delapan karakter, huruf besar atau angka. Tanpa argumen kedua, pairing code memakai nilai bawaan.

```js
const code = await sock.requestPairingCode('6281234567890', 'ARKANA01')
console.log('Pairing code:', code)
```

Contoh di atas menghasilkan kode `ARKANA01`. Kode seperti `arkana01`, `ARKANA0`, atau `ARKANA001` ditolak.

## Header media pada tombol interaktif

`image` atau `video` di level atas akan di-upload dan ditempatkan sebagai `interactiveMessage.header` dengan `hasMediaAttachment: true`. `title`, `subtitle`, `text`, dan `footer` tetap dikirim.

```js
await sock.sendMessage('6281234567890@s.whatsapp.net', {
  image: { url: 'https://example.com/promo.jpg' },
  title: 'Promo hari ini',
  subtitle: 'Berlaku sampai pukul 18.00',
  text: 'Pilih paket yang diinginkan:',
  footer: 'Arkana Store',
  interactiveButtons: [
    { name: 'quick_reply', buttonParamsJson: { display_text: 'Paket A', id: 'paket_a' } }
  ]
})
```

## Album media

`album` menerima minimal dua item `{ image, caption }` atau `{ video, caption }`. Pesan induk `albumMessage` dikirim lebih dulu, lalu setiap media dikirim sebagai anak dengan `messageAssociation` bertipe `MEDIA_ALBUM`.

```js
await sock.sendMessage('6281234567890@s.whatsapp.net', {
  album: [
    { image: { url: 'https://example.com/1.jpg' }, caption: 'Gambar pertama' },
    { image: { url: 'https://example.com/2.jpg' }, caption: 'Gambar kedua' },
    { video: { url: 'https://example.com/3.mp4' }, caption: 'Video ketiga' }
  ]
})
```

## Mentions otomatis

Nomor berformat `@628...` atau `@<5-16 digit>` di `text`, `caption`, dan teks kartu interaktif otomatis ditambahkan ke `contextInfo.mentionedJid`. JID dari `mentions` manual tetap dipertahankan dan tidak dobel.

```js
await sock.sendMessage('6281234567890@s.whatsapp.net', {
  text: 'Halo @6281234567890 dan @6289876543210, cek jadwalnya.'
})
```

## Forwarded dan channel badge

`isForwarded: true` menghasilkan `forwardingScore` bawaan `1`. Jika `channelBadge.jid` adalah JID newsletter, nilainya `999` dan informasi channel disertakan. `contextInfo` yang sudah ada tidak ditimpa.

```js
await sock.sendMessage('6281234567890@s.whatsapp.net', {
  text: 'Kutipan dari channel',
  isForwarded: true,
  channelBadge: {
    jid: '120363012345678901@newsletter',
    name: 'Arkana News',
    serverMessageId: 42
  }
})
```

## Video note dan voice note view once

`videoNote` menghasilkan `ptvMessage`. Audio dengan `ptt: true` dan `viewOnce: true` dibungkus ke `viewOnceMessageV2` dengan `audioMessage.viewOnce = true`.

```js
import { readFileSync } from 'fs'

await sock.sendMessage('6281234567890@s.whatsapp.net', {
  videoNote: readFileSync('./cat.mp4'),
  jpegThumbnail: readFileSync('./cat-thumb.jpg')
})

await sock.sendMessage('6281234567890@s.whatsapp.net', {
  audio: readFileSync('./rahasia.ogg'),
  ptt: true,
  viewOnce: true,
  seconds: 5,
  waveform: Buffer.alloc(0)
})
```

## Poll dan event

`poll` menghasilkan `pollCreationMessageV3`. `messageSecret` 32 byte dibuat otomatis bila tidak diberikan.

```js
await sock.sendMessage('6281234567890@s.whatsapp.net', {
  poll: {
    name: 'Pilih jadwal',
    values: ['Sabtu', 'Minggu'],
    selectableCount: 1
  }
})
```

`event` menerima `startTime` dan `endTime` dalam detik Unix, milidetik, `Date`, atau string ISO. `extraGuestsAllowed` diteruskan ke `eventMessage`.

```js
await sock.sendMessage('6281234567890@s.whatsapp.net', {
  event: {
    name: 'Rapat Arkana',
    description: 'Diskusi rilis 1.2.0',
    startTime: Math.floor(Date.now() / 1000) + 3600,
    endTime: Math.floor(Date.now() / 1000) + 7200,
    extraGuestsAllowed: true,
    location: { degreesLatitude: -6.2, degreesLongitude: 106.8 }
  }
})
```

## reply dan downloadMedia

`sock.reply` menerima string atau objek pesan, lalu meneruskan `quoted` dan opsi lain ke `sendMessage`. Shortcut seperti `interactiveButtons`, `cards`, `album`, `channelBadge`, `isForwarded`, `videoNote`, dan auto-mention tetap bekerja.

```js
await sock.reply(jid, 'Balasan cepat', msg)

await sock.reply(jid, {
  text: 'Pilih @6281234567890',
  interactiveButtons: [
    { name: 'quick_reply', buttonParamsJson: { display_text: 'Oke', id: 'oke' } }
  ]
}, msg, { ephemeralExpiration: 86400 })
```

`sock.downloadMedia` membuka wrapper `ephemeralMessage`, `viewOnceMessage`, `viewOnceMessageV2`, `viewOnceMessageV2Extension`, dan `documentWithCaptionMessage`, lalu mengembalikan `Buffer`. Objek `quotedMessage` juga diterima.

```js
sock.ev.on('messages.upsert', async ({ messages }) => {
  for (const msg of messages) {
    const target = msg.quotedMessage || msg
    if (!target.message) continue
    try {
      const buffer = await sock.downloadMedia(target)
      console.log('Media diterima:', buffer.length, 'byte')
    } catch (error) {
      if (!/does not contain media/.test(error.message)) throw error
    }
  }
})
```

## Helper extractMessage

`extractMessage` membuka wrapper pesan dan mengembalikan `{ type, message, text, selectedId, flowParams, quoted }`. Balasan tombol native flow dibaca dari `paramsJson`, termasuk `listResponseMessage`, `buttonsResponseMessage`, dan `templateButtonReplyMessage`.

```js
import makeWASocket, { extractMessage } from 'arkana-baileys'

sock.ev.on('messages.upsert', ({ messages }) => {
  for (const msg of messages) {
    const info = extractMessage(msg)
    if (info.selectedId) {
      console.log('Tombol dipilih:', info.selectedId, info.flowParams)
    }
    else if (info.text) {
      console.log('Teks:', info.text)
    }
  }
})
```

## Auto-LID, message store, dan dekripsi vote poll

Socket menyimpan pasangan `lid <-> pn` dari `contacts.upsert`, `contacts.update`, `chats.upsert`, dan `messages.upsert`. Target `@lid` pada `sendMessage`, `reply`, dan `relayMessage` diterjemahkan otomatis ke `@s.whatsapp.net`.

`makeWASocket` juga memasang message store LRU 500 pesan sebagai `getMessage` default. Store ini dipakai ulang oleh retry dan dekripsi `pollUpdateMessage`. `extractMessage` mengembalikan `pollVotes` dan `selectedOptions` berisi nama opsi hasil dekripsi.

```js
const info = extractMessage(msg)
if (info.type === 'pollUpdateMessage') {
  console.log('Vote:', info.pollVotes)
}
```

## Status mention, hidetag, dan readViewOnce

```js
await sock.sendStatusMention({ text: 'Halo semua!' }, [
  '6281234567890@s.whatsapp.net',
  '120363012345678901@g.us'
], { backgroundColor: '#FF5500', font: 2 })

await sock.hidetag('120363012345678901@g.us', { text: 'Halo @all' })

const media = await sock.readViewOnce(msg)
console.log(media.type, media.mimetype, media.buffer.length)
```

## fakeQuote, channelBadge opsi, dan tombol native flow baru

`channelBadge: true` atau `{ newsletterJid, newsletterName }` pada opsi `sendMessage`/`reply` menyuntikkan badge forwarded ke teks, media, maupun `interactiveMessage`. `fakeQuote` membuat quote sintetis tanpa membungkus teks ke `viewOnceMessage`.

```js
await sock.reply(jid, { text: 'Balasan dengan badge' }, msg, {
  channelBadge: true,
  fakeQuote: {
    participant: jid,
    stanzaId: 'AC441757748997A22108E588876D4B28',
    quotedMessage: { conversation: 'Pesan asli' }
  }
})

await sock.sendMessage(jid, {
  text: 'Pilih:',
  interactiveButtons: [
    { type: 'cta_copy', display_text: 'Salin kode', copy_code: 'ARKANA50', id: 'copy' },
    { type: 'cta_call', display_text: 'Telepon', phone_number: '+6281234567890' },
    { type: 'send_location', display_text: 'Kirim lokasi' }
  ]
})
```

## Permintaan pembayaran

```js
await sock.sendMessage(jid, {
  requestPayment: {
    currency: 'IDR',
    amount: 50000,
    from: jid,
    note: 'Tagihan langganan'
  }
})
```

`amount1000` dihitung otomatis sebagai `amount * 1000` dan `note` menjadi `noteMessage`.

## Catatan

Versi `1.3.0-mod` memakai `WAProto/index.proto` sebagai skema kanonik. Schema mencakup `pollCreationMessageV3`, `eventMessage.extraGuestsAllowed`, `messageAssociation` untuk album, dan field tambahan `accessibilityLabel` serta `flowId` pada pesan interaktif.

Proyek ini bukan produk resmi WhatsApp. Gunakan sesuai kebijakan layanan WhatsApp dan tanggung jawab Anda sendiri. Kode ini dilanjutkan dari Baileys oleh WhiskeySockets di bawah lisensi MIT.
