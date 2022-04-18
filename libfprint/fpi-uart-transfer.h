/*
 * FPrint UART transfer handling
 * Copyright (C) 2022 Canyon Shapiro <cshapiro@rollins.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include "fpi-compat.h"
#include "fpi-device.h"

G_BEGIN_DECLS

typedef struct _FpiUartTransfer FpiUartTransfer;
typedef struct _FpiSsm         FpiSsm;

typedef void (*FpiUartTransferCallback)(FpiUartTransfer *transfer,
                                       FpDevice       *dev,
                                       gpointer        user_data,
                                       GError         *error);

struct _FpiUartTransfer
{
  /*< public >*/
  FpDevice *device;

  FpiSsm   *ssm;

  gssize    length_wr;
  gssize    length_rd;

  guchar   *buffer_wr;
  guchar   *buffer_rd;

  /*< private >*/
  guint ref_count;

  int   uartdev_fd;

  /* Callbacks */
  gpointer               user_data;
  FpiUartTransferCallback callback;

  /* Data free function */
  GDestroyNotify free_buffer_wr;
  GDestroyNotify free_buffer_rd;
};

GType              fpi_uart_transfer_get_type (void) G_GNUC_CONST;
FpiUartTransfer     *fpi_uart_transfer_new (FpDevice *device,
                                          int       uartdev_fd);
FpiUartTransfer     *fpi_uart_transfer_ref (FpiUartTransfer *self);
void               fpi_uart_transfer_unref (FpiUartTransfer *self);

void               fpi_uart_transfer_write (FpiUartTransfer *transfer,
                                           gsize           length);

FP_GNUC_ACCESS (read_only, 2, 3)
void               fpi_uart_transfer_write_full (FpiUartTransfer *transfer,
                                                guint8         *buffer,
                                                gsize           length,
                                                GDestroyNotify  free_func);

void               fpi_uart_transfer_read (FpiUartTransfer *transfer,
                                          gsize           length);

FP_GNUC_ACCESS (write_only, 2, 3)
void               fpi_uart_transfer_read_full (FpiUartTransfer *transfer,
                                               guint8         *buffer,
                                               gsize           length,
                                               GDestroyNotify  free_func);

void               fpi_uart_transfer_submit (FpiUartTransfer        *transfer,
                                            GCancellable          *cancellable,
                                            FpiUartTransferCallback callback,
                                            gpointer               user_data);

gboolean           fpi_uart_transfer_submit_sync (FpiUartTransfer *transfer,
                                                 GError        **error);


G_DEFINE_AUTOPTR_CLEANUP_FUNC (FpiUartTransfer, fpi_uart_transfer_unref)

G_END_DECLS
