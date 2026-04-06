/*
 * PicoMSO - Mixed Signal Oscilloscope
 * Copyright (C) 2026 Daniel Gorbea
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */

/*
 * PicoMSO transport abstraction layer – helper implementations (Phase 0).
 *
 * This file provides the four thin wrappers declared in transport.h.
 * There is no hardware dependency: the functions delegate all real work
 * to the function pointers in transport_interface_t that the caller
 * supplies at initialisation time.
 */

#include "transport.h"

/* -----------------------------------------------------------------------
 * transport_init
 * ----------------------------------------------------------------------- */

transport_result_t transport_init(transport_ctx_t             *ctx,
                                  const transport_interface_t *iface,
                                  void                        *user_data)
{
    if (ctx == NULL || iface == NULL) {
        return TRANSPORT_ERR_NULL;
    }
    if (iface->send == NULL || iface->receive == NULL) {
        return TRANSPORT_ERR_NULL;
    }

    ctx->iface     = iface;
    ctx->user_data = user_data;
    return TRANSPORT_OK;
}

/* -----------------------------------------------------------------------
 * transport_is_ready
 * ----------------------------------------------------------------------- */

bool transport_is_ready(const transport_ctx_t *ctx)
{
    if (ctx == NULL || ctx->iface == NULL) {
        return false;
    }
    if (ctx->iface->is_ready == NULL) {
        /* No readiness check provided – treat as always ready. */
        return true;
    }
    return ctx->iface->is_ready(ctx->user_data);
}

/* -----------------------------------------------------------------------
 * transport_send
 * ----------------------------------------------------------------------- */

transport_result_t transport_send(const transport_ctx_t *ctx,
                                  const uint8_t         *buf,
                                  size_t                 len)
{
    if (ctx == NULL || ctx->iface == NULL || ctx->iface->send == NULL) {
        return TRANSPORT_ERR_NULL;
    }
    if (buf == NULL) {
        return TRANSPORT_ERR_NULL;
    }
    if (len == 0) {
        return TRANSPORT_OK;
    }
    return ctx->iface->send(ctx->user_data, buf, len);
}

/* -----------------------------------------------------------------------
 * transport_receive
 * ----------------------------------------------------------------------- */

transport_result_t transport_receive(const transport_ctx_t *ctx,
                                     uint8_t               *buf,
                                     size_t                 len,
                                     size_t                *bytes_read)
{
    if (ctx == NULL || ctx->iface == NULL || ctx->iface->receive == NULL) {
        return TRANSPORT_ERR_NULL;
    }
    if (buf == NULL || bytes_read == NULL) {
        return TRANSPORT_ERR_NULL;
    }
    *bytes_read = 0;
    if (len == 0) {
        return TRANSPORT_OK;
    }
    return ctx->iface->receive(ctx->user_data, buf, len, bytes_read);
}
