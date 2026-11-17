/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

class MainWindow;

/* Modal partition-creation dialog. On success, `slot` receives the new slot. */
bool dialog_new_partition(MainWindow *owner, int *slot);
/* Modal image-creation dialog; returns the index of the new disk, or -1. */
int dialog_new_image(MainWindow *owner);
