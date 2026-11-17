/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

class MainWindow;

/* Modal partition-creation dialog. On success, `slot` receives the new slot. */
bool dialog_new_partition(MainWindow *owner, int *slot);
/* Modal image-creation dialog; returns the index of the new disk, or -1. */
int dialog_new_image(MainWindow *owner);
/* Modal About window with the application icon and a short feature summary. */
void dialog_about(MainWindow *owner);
