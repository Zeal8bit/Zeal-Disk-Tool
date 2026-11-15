/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

/**
 * ============================================================================
 *                              OS SPECIFIC CODE
 * ============================================================================
 */

/**
 * OPTIONAL OS FEATURES
 */

/**
 * @brief Initializes the progress bar for disk operations.
 *
 * This function sets up the progress bar to provide visual feedback
 * to the user about the progress of a disk operation.
 */
void disk_init_progress_bar(void);

/**
 * @brief Updates the progress bar with the current progress.
 *
 * @param percent The current progress percentage (0 to 100).
 * This function updates the progress bar to reflect the given
 * percentage, providing real-time feedback to the user.
 */
void disk_update_progress_bar(int percent);

/**
 * @brief Destroys the progress bar after the operation is complete.
 *
 * This function cleans up any resources associated with the progress bar
 * and removes it from the display once the disk operation is finished.
 */
void disk_destroy_progress_bar(void);
