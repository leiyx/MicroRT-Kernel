#include "mrtk_ringbuffer.h"
#include "mrtk_errno.h"

mrtk_void_t mrtk_rb_init(mrtk_rb_t *rb, mrtk_u8_t *pool, mrtk_size_t size)
{
    rb->buffer    = pool;
    rb->size      = size;
    rb->read_idx  = 0;
    rb->write_idx = 0;
}

mrtk_size_t mrtk_rb_read(mrtk_rb_t *rb, mrtk_u8_t *ptr, mrtk_size_t size)
{
    mrtk_u16_t data_len;
    mrtk_u16_t read_len;
    mrtk_u16_t first_chunk;

    data_len = mrtk_rb_get_len(rb);

    /* 如果缓冲区为空，返回 0 */
    if (data_len == 0) {
        return 0;
    }

    /* 实际可读长度为请求长度和可用长度的较小值 */
    read_len = (size < data_len) ? size : data_len;

    /* 计算第一段可读数据长度（直到缓冲区末尾） */
    first_chunk = rb->size - rb->read_idx;

    if (read_len <= first_chunk) {
        /* 数据不需要跨越缓冲区末尾 */
        for (mrtk_u16_t i = 0; i < read_len; i++) {
            ptr[i] = rb->buffer[rb->read_idx + i];
        }
        rb->read_idx = (rb->read_idx + read_len) % rb->size;
    } else {
        /* 数据跨越缓冲区末尾，分两次读取 */
        /* 读取第一段：从读指针到缓冲区末尾 */
        for (mrtk_u16_t i = 0; i < first_chunk; i++) {
            ptr[i] = rb->buffer[rb->read_idx + i];
        }
        /* 读取第二段：从缓冲区开头 */
        mrtk_u16_t second_chunk = read_len - first_chunk;
        for (mrtk_u16_t i = 0; i < second_chunk; i++) {
            ptr[first_chunk + i] = rb->buffer[i];
        }
        rb->read_idx = second_chunk;
    }

    return read_len;
}

mrtk_size_t mrtk_rb_write(mrtk_rb_t *rb, const mrtk_u8_t *ptr, mrtk_size_t size)
{
    mrtk_u16_t free_len;
    mrtk_u16_t write_len;
    mrtk_u16_t first_chunk;

    free_len = mrtk_rb_get_free(rb);

    /* 如果缓冲区已满，返回 0 */
    if (free_len == 0) {
        return 0;
    }

    /* 实际可写长度为请求长度和可用空间的较小值 */
    write_len = (size < free_len) ? size : free_len;

    /* 计算第一段可写空间（直到缓冲区末尾） */
    first_chunk = rb->size - rb->write_idx;

    if (write_len <= first_chunk) {
        /* 数据不需要跨越缓冲区末尾 */
        for (mrtk_u16_t i = 0; i < write_len; ++i) {
            rb->buffer[rb->write_idx + i] = ptr[i];
        }
        rb->write_idx = (rb->write_idx + write_len) % rb->size;
    } else {
        /* 数据跨越缓冲区末尾，分两次写入 */
        /* 写入第一段：从写指针到缓冲区末尾 */
        for (mrtk_u16_t i = 0; i < first_chunk; i++) {
            rb->buffer[rb->write_idx + i] = ptr[i];
        }
        /* 写入第二段：从缓冲区开头 */
        mrtk_u16_t second_chunk = write_len - first_chunk;
        for (mrtk_u16_t i = 0; i < second_chunk; i++) {
            rb->buffer[i] = ptr[first_chunk + i];
        }
        rb->write_idx = second_chunk;
    }

    return write_len;
}

mrtk_err_t mrtk_rb_putc(mrtk_rb_t *rb, mrtk_char_t ch)
{
    /* 检查缓冲区是否已满 */
    if (mrtk_rb_is_full(rb->read_idx, rb->write_idx, rb->size)) {
        return MRTK_EFULL;
    }

    rb->buffer[rb->write_idx] = (mrtk_u8_t) ch;
    rb->write_idx             = (rb->write_idx + 1) % rb->size;

    return MRTK_EOK;
}

mrtk_err_t mrtk_rb_getc(mrtk_rb_t *rb, mrtk_char_t *ch)
{
    /* 检查缓冲区是否为空 */
    if (mrtk_rb_is_empty(rb->read_idx, rb->write_idx)) {
        return MRTK_EEMPTY;
    }

    *ch          = (mrtk_char_t) rb->buffer[rb->read_idx];
    rb->read_idx = (rb->read_idx + 1) % rb->size;

    return MRTK_EOK;
}
