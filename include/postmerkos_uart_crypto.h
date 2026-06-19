#ifndef POSTMERKOS_UART_CRYPTO_H
#define POSTMERKOS_UART_CRYPTO_H

typedef unsigned char pmos_u8;
typedef unsigned int pmos_u32;

typedef struct {
    pmos_u32 state[8];
    pmos_u32 bit_count_low;
    pmos_u32 bit_count_high;
    pmos_u8 block[64];
    pmos_u32 block_used;
} pmos_sha256_ctx;

static pmos_u32 pmos_rotr32(pmos_u32 value, unsigned int shift)
{
    return (value >> shift) | (value << (32u - shift));
}

static pmos_u32 pmos_crc32_update(pmos_u32 crc, const pmos_u8 *data, pmos_u32 length)
{
    pmos_u32 i;
    while (length--) {
        crc ^= (pmos_u32)*data++;
        for (i = 0; i < 8u; i++)
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

static pmos_u32 pmos_crc32(const pmos_u8 *data, pmos_u32 length)
{
    return ~pmos_crc32_update(0xffffffffu, data, length);
}

static const pmos_u32 pmos_sha256_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,
    0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,
    0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,
    0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,
    0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,
    0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static void pmos_sha256_transform(pmos_sha256_ctx *ctx, const pmos_u8 block[64])
{
    pmos_u32 w[64];
    pmos_u32 a,b,c,d,e,f,g,h,t1,t2,s0,s1,ch,maj;
    pmos_u32 i;
    for (i = 0; i < 16u; i++) {
        pmos_u32 j = i * 4u;
        w[i] = ((pmos_u32)block[j] << 24) |
               ((pmos_u32)block[j + 1u] << 16) |
               ((pmos_u32)block[j + 2u] << 8) |
               (pmos_u32)block[j + 3u];
    }
    for (i = 16u; i < 64u; i++) {
        s0 = pmos_rotr32(w[i - 15u], 7u) ^ pmos_rotr32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
        s1 = pmos_rotr32(w[i - 2u], 17u) ^ pmos_rotr32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i = 0; i < 64u; i++) {
        s1 = pmos_rotr32(e, 6u) ^ pmos_rotr32(e, 11u) ^ pmos_rotr32(e, 25u);
        ch = (e & f) ^ ((~e) & g);
        t1 = h + s1 + ch + pmos_sha256_k[i] + w[i];
        s0 = pmos_rotr32(a, 2u) ^ pmos_rotr32(a, 13u) ^ pmos_rotr32(a, 22u);
        maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void pmos_sha256_init(pmos_sha256_ctx *ctx)
{
    ctx->state[0]=0x6a09e667u; ctx->state[1]=0xbb67ae85u;
    ctx->state[2]=0x3c6ef372u; ctx->state[3]=0xa54ff53au;
    ctx->state[4]=0x510e527fu; ctx->state[5]=0x9b05688cu;
    ctx->state[6]=0x1f83d9abu; ctx->state[7]=0x5be0cd19u;
    ctx->bit_count_low=0u; ctx->bit_count_high=0u; ctx->block_used=0u;
}

static void pmos_sha256_add_bits(pmos_sha256_ctx *ctx, pmos_u32 bytes)
{
    pmos_u32 old = ctx->bit_count_low;
    pmos_u32 bits = bytes << 3;
    ctx->bit_count_low += bits;
    if (ctx->bit_count_low < old) ctx->bit_count_high++;
    ctx->bit_count_high += bytes >> 29;
}

static void pmos_sha256_update(pmos_sha256_ctx *ctx, const pmos_u8 *data, pmos_u32 length)
{
    pmos_u32 take, i;
    pmos_sha256_add_bits(ctx, length);
    while (length) {
        take = 64u - ctx->block_used;
        if (take > length) take = length;
        for (i = 0; i < take; i++) ctx->block[ctx->block_used + i] = data[i];
        ctx->block_used += take;
        data += take;
        length -= take;
        if (ctx->block_used == 64u) {
            pmos_sha256_transform(ctx, ctx->block);
            ctx->block_used = 0u;
        }
    }
}

static void pmos_sha256_final(pmos_sha256_ctx *ctx, pmos_u8 out[32])
{
    pmos_u32 i;
    ctx->block[ctx->block_used++] = 0x80u;
    if (ctx->block_used > 56u) {
        while (ctx->block_used < 64u) ctx->block[ctx->block_used++] = 0u;
        pmos_sha256_transform(ctx, ctx->block);
        ctx->block_used = 0u;
    }
    while (ctx->block_used < 56u) ctx->block[ctx->block_used++] = 0u;
    ctx->block[56]=(pmos_u8)(ctx->bit_count_high>>24);
    ctx->block[57]=(pmos_u8)(ctx->bit_count_high>>16);
    ctx->block[58]=(pmos_u8)(ctx->bit_count_high>>8);
    ctx->block[59]=(pmos_u8)ctx->bit_count_high;
    ctx->block[60]=(pmos_u8)(ctx->bit_count_low>>24);
    ctx->block[61]=(pmos_u8)(ctx->bit_count_low>>16);
    ctx->block[62]=(pmos_u8)(ctx->bit_count_low>>8);
    ctx->block[63]=(pmos_u8)ctx->bit_count_low;
    pmos_sha256_transform(ctx, ctx->block);
    for (i = 0; i < 8u; i++) {
        out[i*4u]=(pmos_u8)(ctx->state[i]>>24);
        out[i*4u+1u]=(pmos_u8)(ctx->state[i]>>16);
        out[i*4u+2u]=(pmos_u8)(ctx->state[i]>>8);
        out[i*4u+3u]=(pmos_u8)ctx->state[i];
    }
}

static int pmos_digest_equal(const pmos_u8 *a, const pmos_u8 *b, pmos_u32 length)
{
    pmos_u8 difference = 0u;
    pmos_u32 i;
    for (i = 0; i < length; i++) difference |= (pmos_u8)(a[i] ^ b[i]);
    return difference == 0u;
}

#endif
