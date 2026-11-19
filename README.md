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

## Catatan

Proyek ini bukan produk resmi WhatsApp. Gunakan sesuai kebijakan layanan WhatsApp dan tanggung jawab Anda sendiri. Kode ini dilanjutkan dari Baileys oleh WhiskeySockets di bawah lisensi MIT.
