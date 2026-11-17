/**
 * SPDX-FileCopyrightText: 2026 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gui/Dialogs.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Slider.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_Value_Input.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>

#include <algorithm>
#include <cstdio>
#include <string>

#include "app_icon_png.h"
#include "app_version.h"
#include "app_wordmark.h"
#include "gui/MainWindow.hpp"
#include "gui/ModernStyle.hpp"
#include "gui/Utils.hpp"

constexpr uint64_t kMinPartitionBytes = 8 * KB;

/* Dialogs use flat rounded fields and the modern button from ModernStyle
 * instead of the scheme's beveled boxes, which look out of place next to the
 * custom-drawn window chrome. */
static void style_field(Fl_Widget *widget)
{
    widget->box(modern::field_box());
    widget->color(modern::color(modern::Palette::kPanel));
    widget->selection_color(modern::color(modern::Palette::kAccentSoft));
}

static void style_choice(Fl_Choice *choice)
{
    style_field(choice);
    choice->textsize(12);
    choice->textfont(FL_HELVETICA);
    choice->textcolor(modern::color(modern::Palette::kDark));
}

static void style_input(Fl_Input *input)
{
    style_field(input);
    input->textsize(12);
    input->textfont(FL_HELVETICA);
    input->textcolor(modern::color(modern::Palette::kDark));
    input->cursor_color(modern::color(modern::Palette::kAccent));
}

static void style_value_input(Fl_Value_Input *input)
{
    style_field(input);
    input->input.textsize(12);
    input->input.textfont(FL_HELVETICA);
    input->input.textcolor(modern::color(modern::Palette::kDark));
    input->input.cursor_color(modern::color(modern::Palette::kAccent));
}

static void center_on(Fl_Window *dialog, Fl_Window *owner)
{
    if (owner != nullptr) {
        dialog->position(owner->x() + (owner->w() - dialog->w()) / 2,
                         owner->y() + (owner->h() - dialog->h()) / 2);
    }
}

static void close_cb(Fl_Widget *widget, void *)
{
    widget->hide();
}

/* ------------------------------------------------------------------------- */
/*                        Create a new partition                             */
/* ------------------------------------------------------------------------- */

class NewPartitionDialog : public Fl_Window {
public:
    explicit NewPartitionDialog(MainWindow *owner)
        : Fl_Window(440, 360, "Create a new partition"), owner_(owner), disk_(owner->current_disk())
    {
        set_modal();
        color(modern::color(modern::Palette::kWindow));

        info_ = new Fl_Box(FL_NO_BOX, 20, 14, 400, 40, "");
        info_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        info_->labelsize(11);
        info_->labelcolor(modern::color(modern::Palette::kGray));

        Fl_Box *type_caption = make_caption("Type", 64);
        type_ = new Fl_Choice(120, 64, 180, 24);
        type_->add("ZealFS v2");
        type_->value(0);
        type_->deactivate();
        style_choice(type_);
        (void) type_caption;

        make_caption("Size", 100);
        size_ = new Fl_Value_Input(120, 100, 120, 24);
        size_->callback(size_cb, this);
        size_->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY);
        style_value_input(size_);
        unit_ = new Fl_Choice(248, 100, 80, 24);
        unit_->add("KiB");
        unit_->add("MiB");
        unit_->value(1);
        unit_->callback(unit_cb, this);
        style_choice(unit_);

        slider_ = new Fl_Slider(120, 132, 208, 20);
        slider_->type(FL_HOR_NICE_SLIDER);
        slider_->callback(slider_cb, this);
        slider_->when(FL_WHEN_CHANGED | FL_WHEN_RELEASE);
        slider_->color(modern::color(modern::Palette::kPanelAlt));
        slider_->selection_color(modern::color(modern::Palette::kAccent));

        make_caption("Alignment", 162);
        alignment_ = new Fl_Choice(120, 162, 160, 24);
        alignment_->add("512 bytes");
        alignment_->add("1 MiB");
        alignment_->value(1);
        alignment_->callback(alignment_cb, this);
        style_choice(alignment_);

        make_caption("Address", 196);
        address_ = new Fl_Output(120, 196, 160, 24);
        style_field(address_);
        address_->color(modern::color(modern::Palette::kPanelAlt));
        address_->textsize(12);
        address_->textfont(FL_HELVETICA);
        address_->textcolor(modern::color(modern::Palette::kDark2));

        message_ = new Fl_Box(FL_NO_BOX, 20, 228, 400, 32, "");
        message_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        message_->labelsize(11);
        message_->labelcolor(modern::color(modern::Palette::kWarning));

        Fl_Box *note =
            new Fl_Box(FL_NO_BOX, 20, 282, 400, 18, "The partition is created when the changes are applied.");
        note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        note->labelsize(11);
        note->labelcolor(modern::color(modern::Palette::kGray));

        create_ = new modern::ModernButton(230, 310, 90, 30, "Create", true);
        create_->callback(create_cb, this);
        auto *cancel = new modern::ModernButton(330, 310, 90, 30, "Cancel");
        cancel->callback(close_cb);

        end();
        callback(close_cb);

        refresh_limits();
    }

    bool created() const { return created_; }
    int slot() const { return slot_; }

    int handle(int event) override
    {
        if (event == FL_KEYBOARD && Fl::event_key() == FL_Escape) {
            hide();
            return 1;
        }
        return Fl_Window::handle(event);
    }

private:
    Fl_Box *make_caption(const char *label, int y)
    {
        auto *caption = new Fl_Box(FL_NO_BOX, 20, y, 90, 24, label);
        caption->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        caption->labelsize(12);
        caption->labelcolor(modern::color(modern::Palette::kDark));
        return caption;
    }

    uint32_t alignment_bytes() const
    {
        return alignment_->value() == 0 ? 512u : 1048576u;
    }

    double unit_scale() const
    {
        return unit_->value() == 1 ? 1024.0 : 1.0;
    }

    void refresh_limits()
    {
        max_bytes_ = disk_max_partition_size(disk_, alignment_bytes(), &next_address_);
        max_kib_ = std::max<uint64_t>(max_bytes_ / KB, 0);
        const uint64_t min_kib = kMinPartitionBytes / KB;

        if (max_kib_ < min_kib) {
            message_->copy_label("There is not enough free space to create a partition on this disk.");
            create_->deactivate();
            size_->deactivate();
            slider_->deactivate();
            address_->value("");
            info_->copy_label("Free space is too small for a ZealFS v2 partition.");
            return;
        }

        syncing_ = true;
        const bool use_mib = max_kib_ >= 1024;
        unit_->value(use_mib ? 1 : 0);
        size_kib_ = max_kib_;
        update_size_bounds(min_kib);
        size_->step(unit_->value() == 1 ? 0.25 : 1.0);
        size_->value((double) size_kib_ / unit_scale());
        slider_->bounds((double) min_kib, (double) max_kib_);
        slider_->step(1);
        slider_->value((double) size_kib_);
        syncing_ = false;

        char info[192];
        std::snprintf(info,
                      sizeof(info),
                      "Maximum new partition: %s, starting at 0x%08llX.\n"
                      "The new partition is formatted as ZealFS v2.",
                      utils::format_size(max_bytes_).c_str(),
                      (unsigned long long) next_address_);
        info_->copy_label(info);
        update_validation();
    }

    /* Fl_Value_Input displays KiB or MiB, so its bounds follow the unit. */
    void update_size_bounds(uint64_t min_kib)
    {
        const double scale = unit_scale();
        size_->bounds((double) min_kib / scale, (double) max_kib_ / scale);
    }

    void update_address()
    {
        char address[32];
        std::snprintf(address, sizeof(address), "0x%08llX", (unsigned long long) next_address_);
        address_->value(address);
    }

    void update_validation()
    {
        const uint64_t min_kib = kMinPartitionBytes / KB;
        const bool enough_space = max_kib_ >= min_kib;
        const bool size_ok = size_kib_ >= min_kib && size_kib_ * KB <= max_bytes_;
        if (!enough_space) {
            create_->deactivate();
            return;
        }
        if (size_kib_ < min_kib) {
            message_->copy_label("The minimum partition size is 8 KiB.");
        } else if (size_kib_ * KB > max_bytes_) {
            message_->copy_label("The selected size is larger than the largest free block.");
        } else {
            message_->copy_label("");
        }
        if (size_ok) {
            create_->activate();
        } else {
            create_->deactivate();
        }
        update_address();
    }

    void sync_from_input()
    {
        if (syncing_) {
            return;
        }
        syncing_ = true;
        const double value = size_->value();
        size_kib_ = value > 0 ? (uint64_t) (value * unit_scale()) : 0;
        slider_->value((double) std::min<uint64_t>(size_kib_, max_kib_));
        syncing_ = false;
        update_validation();
    }

    static void size_cb(Fl_Widget *, void *data)
    {
        static_cast<NewPartitionDialog *>(data)->sync_from_input();
    }

    static void slider_cb(Fl_Widget *, void *data)
    {
        auto *dialog = static_cast<NewPartitionDialog *>(data);
        if (dialog->syncing_) {
            return;
        }
        dialog->syncing_ = true;
        dialog->size_kib_ = (uint64_t) dialog->slider_->value();
        dialog->size_->value((double) dialog->size_kib_ / dialog->unit_scale());
        dialog->syncing_ = false;
        dialog->update_validation();
    }

    static void unit_cb(Fl_Widget *, void *data)
    {
        auto *dialog = static_cast<NewPartitionDialog *>(data);
        dialog->syncing_ = true;
        dialog->update_size_bounds(kMinPartitionBytes / KB);
        dialog->size_->step(dialog->unit_->value() == 1 ? 0.25 : 1.0);
        dialog->size_->value((double) dialog->size_kib_ / dialog->unit_scale());
        dialog->syncing_ = false;
    }

    static void alignment_cb(Fl_Widget *, void *data)
    {
        static_cast<NewPartitionDialog *>(data)->refresh_limits();
    }

    static void create_cb(Fl_Widget *, void *data)
    {
        auto *dialog = static_cast<NewPartitionDialog *>(data);
        if (dialog->disk_ == nullptr || dialog->size_kib_ * KB > dialog->max_bytes_) {
            return;
        }
        uint64_t bytes = dialog->size_kib_ * KB;
        bytes -= bytes % DISK_SECTOR_SIZE;
        int slot = -1;
        if (!disk_partition_create(dialog->disk_, bytes, dialog->alignment_bytes(), &slot)) {
            fl_alert("%s", dialog->owner_->status_text().c_str());
            return;
        }
        dialog->created_ = true;
        dialog->slot_ = slot;
        dialog->hide();
    }

    MainWindow *owner_;
    disk_info_t *disk_;
    Fl_Box *info_ = nullptr;
    Fl_Choice *type_ = nullptr;
    Fl_Value_Input *size_ = nullptr;
    Fl_Choice *unit_ = nullptr;
    Fl_Slider *slider_ = nullptr;
    Fl_Choice *alignment_ = nullptr;
    Fl_Output *address_ = nullptr;
    Fl_Box *message_ = nullptr;
    Fl_Button *create_ = nullptr;

    uint64_t max_bytes_ = 0;
    uint64_t max_kib_ = 0;
    uint64_t size_kib_ = 0;
    uint64_t next_address_ = 0;
    int slot_ = -1;
    bool created_ = false;
    bool syncing_ = false;
};

/* ------------------------------------------------------------------------- */
/*                        Create a new disk image                            */
/* ------------------------------------------------------------------------- */

class NewImageDialog : public Fl_Window {
public:
    explicit NewImageDialog(MainWindow *owner) : Fl_Window(480, 330, "Create a disk image"), owner_(owner)
    {
        set_modal();
        color(modern::color(modern::Palette::kWindow));

        Fl_Box *location_caption = caption("Location", 24);
        (void) location_caption;
        path_ = new Fl_Input(108, 24, 258, 24);
        path_->value("disk.img");
        style_input(path_);
        browse_ = new modern::ModernButton(374, 24, 86, 24, "Browse...");
        browse_->callback(browse_cb, this);

        caption("Size", 64);
        size_ = new Fl_Choice(108, 64, 180, 24);
        int count = 0;
        const char *const *sizes = disk_get_partition_size_list(&count);
        for (int index = 0; index < count; ++index) {
            size_->add(sizes[index]);
        }
        size_->value(count > 9 ? 9 : 0);
        size_->callback(size_cb, this);
        style_choice(size_);

        caption("Table", 100);
        table_ = new Fl_Choice(108, 100, 180, 24);
        table_->add("None");
        table_->add("MBR");
        table_->value(1);
        table_->callback(size_cb, this);
        style_choice(table_);

        hint_ = new Fl_Box(FL_NO_BOX, 20, 138, 440, 84, "");
        hint_->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        hint_->labelsize(11);
        hint_->labelcolor(modern::color(modern::Palette::kGray));

        Fl_Box *note =
            new Fl_Box(FL_NO_BOX, 20, 238, 440, 18, "The image file is created when the changes are applied.");
        note->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        note->labelsize(11);
        note->labelcolor(modern::color(modern::Palette::kGray));

        create_ = new modern::ModernButton(270, 280, 90, 30, "Create", true);
        create_->callback(create_cb, this);
        auto *cancel = new modern::ModernButton(370, 280, 90, 30, "Cancel");
        cancel->callback(close_cb);

        end();
        callback(close_cb);
        update_hint();
    }

    int index() const { return index_; }

    int handle(int event) override
    {
        if (event == FL_KEYBOARD && Fl::event_key() == FL_Escape) {
            hide();
            return 1;
        }
        return Fl_Window::handle(event);
    }

private:
    Fl_Box *caption(const char *text, int y)
    {
        auto *box = new Fl_Box(FL_NO_BOX, 20, y, 84, 24, text);
        box->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        box->labelsize(12);
        box->labelcolor(modern::color(modern::Palette::kDark));
        return box;
    }

    void update_hint()
    {
        const uint64_t size = disk_get_size_of_idx(size_->value());
        const bool mbr = table_->value() == 1;
        char hint[512];
        std::snprintf(hint,
                      sizeof(hint),
                      "%s image, %s.\n%s\n\n"
                      "Use \"MBR\" for a partitioned disk with up to four partitions,\n"
                      "or \"None\" for a single raw ZealFS v2 filesystem.",
                      mbr ? "Partitioned (MBR)" : "Raw",
                      utils::format_size(size).c_str(),
                      mbr ? "Create and format partitions after creating the image."
                          : "The filesystem is created directly on the image.");
        hint_->copy_label(hint);
        /* Size the box to the number of explicit lines so the last line is
         * never clipped (the MBR hint has five lines). */
        int lines = 1;
        for (const char *cursor = hint; *cursor != '\0'; ++cursor) {
            if (*cursor == '\n') {
                ++lines;
            }
        }
        hint_->resize(20, 138, 440, std::max(70, lines * 16));
        redraw();
    }

    static void size_cb(Fl_Widget *, void *data)
    {
        static_cast<NewImageDialog *>(data)->update_hint();
    }

    static void browse_cb(Fl_Widget *, void *data)
    {
        auto *dialog = static_cast<NewImageDialog *>(data);
        Fl_Native_File_Chooser chooser;
        chooser.title("Create disk image");
        chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
        chooser.filter("Disk images\t*.img\nAll files\t*");
        chooser.preset_file(dialog->path_->value());
        if (chooser.show() == 0) {
            dialog->path_->value(chooser.filename());
        }
    }

    static void create_cb(Fl_Widget *, void *data)
    {
        auto *dialog = static_cast<NewImageDialog *>(data);
        const char *path = dialog->path_->value();
        if (path == nullptr || *path == 0) {
            fl_alert("Enter a location for the new image.");
            return;
        }
        const uint64_t size = disk_get_size_of_idx(dialog->size_->value());
        const bool mbr = dialog->table_->value() == 1;
        const int index = disk_create_image(dialog->owner_->state(), path, size, mbr);
        if (index < 0) {
            fl_alert("%s", dialog->owner_->status_text().c_str());
            return;
        }
        dialog->index_ = index;
        dialog->hide();
    }

    MainWindow *owner_;
    Fl_Input *path_ = nullptr;
    Fl_Button *browse_ = nullptr;
    Fl_Choice *size_ = nullptr;
    Fl_Choice *table_ = nullptr;
    Fl_Box *hint_ = nullptr;
    Fl_Button *create_ = nullptr;
    int index_ = -1;
};


class AboutDialog : public Fl_Window {
public:
    explicit AboutDialog(MainWindow *owner) : Fl_Window(560, 392, "About Zeal Disk Tool")
    {
        set_modal();
        color(modern::color(modern::Palette::kWindow));
        begin();
        close_ = new modern::ModernButton(0, 0, 10, 10, "Close", true, modern::color(modern::Palette::kZealPurple));
        close_->callback(close_cb, this);
        end();
        close_->resize(w() - 24 - 120, h() - 24 - 36, 120, 36);
        callback(close_cb, this);
        center_on(this, owner);
    }

    void draw() override
    {
        const int pad = 26;
        fl_color(modern::color(modern::Palette::kWindow));
        fl_rectf(0, 0, w(), h());

        /* The 256 px source keeps the icon crisp at this size. Scale the image
         * object once: FLTK's direct draw(x,y,w,h) scaling misreads RGBA data
         * when the requested size differs from the decoded size. */
        static Fl_PNG_Image icon(nullptr, s_about_icon_png, sizeof(s_about_icon_png));
        if (icon.w() != 88) {
            icon.scale(88, 88, 0, 1);
        }
        icon.draw(pad, pad);

        /* Wordmark outlined from the Righteous typeface; drawn a bit smaller
         * than the icon so the version line stays readable. */
        static Fl_SVG_Image wordmark(nullptr,
                                     (const unsigned char *) APP_WORDMARK_SVG,
                                     sizeof(APP_WORDMARK_SVG) - 1);
        const int wordmark_h = 34;
        const int wordmark_w = APP_WORDMARK_WIDTH * wordmark_h / APP_WORDMARK_HEIGHT;
        const float screen_scale = Fl::screen_scale(0) > 0.0f ? Fl::screen_scale(0) : 1.0f;
        const int pixels_w = (int) (wordmark_w * screen_scale + 0.5f);
        const int pixels_h = (int) (wordmark_h * screen_scale + 0.5f);
        if (wordmark.w() != pixels_w || wordmark.h() != pixels_h) {
            wordmark.scale(pixels_w, pixels_h, 0, 1);
        }
        wordmark.draw(pad + 112, pad + 10, wordmark_w, wordmark_h);

        modern::text(std::string("Version ") + VERSION,
                     pad + 112,
                     pad + 56,
                     modern::color(modern::Palette::kGray),
                     11,
                     false);
        modern::text("ZealFS v2 disk and image manager",
                     pad + 112,
                     pad + 76,
                     modern::color(modern::Palette::kGray2),
                     11,
                     false);

        fl_color(modern::color(modern::Palette::kDividerSoft));
        fl_line(pad, pad + 106, w() - pad, pad + 106);

        modern::text_clipped("Create, inspect and update ZealFS v2 disk images and physical "
                             "disks for the Zeal 8-bit Computer. Every change is staged first, "
                             "so you can review it before anything is written.",
                             pad,
                             pad + 122,
                             w() - 2 * pad,
                             54,
                             modern::color(modern::Palette::kDark2),
                             11,
                             false,
                             FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);

        const char *bullets[] = {
            "Create MBR or raw disk images.",
            "Add, format and delete up to four partitions.",
            "Browse, import and export files; drop host files onto the list.",
            "Stage changes, then apply them to an image or a physical card.",
        };
        int y = pad + 192;
        for (const char *bullet : bullets) {
            modern::fill_circle(pad + 3, y - 4, 2, modern::color(modern::Palette::kZealPurple));
            modern::text(bullet, pad + 14, y, modern::color(modern::Palette::kDark2), 11, false);
            y += 22;
        }

        fl_color(modern::color(modern::Palette::kDividerSoft));
        fl_line(pad, h() - 92, w() - pad, h() - 92);
        modern::text("Linux  \xC2\xB7  macOS  \xC2\xB7  Windows",
                     pad,
                     h() - 70,
                     modern::color(modern::Palette::kGray),
                     10,
                     false);
        modern::text("Apache-2.0  \xC2\xB7  Icons by Lucide (ISC)  \xC2\xB7  Built with FLTK",
                     pad,
                     h() - 52,
                     modern::color(modern::Palette::kGray2),
                     10,
                     false);
        modern::text("zeal8bit.com  \xC2\xB7  contact@zeal8bit.com",
                     pad,
                     h() - 34,
                     modern::color(modern::Palette::kGray2),
                     10,
                     false);

        /* The custom background replaces Fl_Window::draw(), so draw the child
         * widgets (the Close button) explicitly. */
        draw_children();
    }

    int handle(int event) override
    {
        if (event == FL_KEYBOARD && (Fl::event_key() == FL_Escape || Fl::event_key() == FL_Enter)) {
            hide();
            return 1;
        }
        return Fl_Window::handle(event);
    }

private:
    static void close_cb(Fl_Widget *, void *data)
    {
        static_cast<AboutDialog *>(data)->hide();
    }

    Fl_Widget *close_ = nullptr;
};

bool dialog_new_partition(MainWindow *owner, int *slot)
{
    if (owner == nullptr || slot == nullptr) {
        return false;
    }
    disk_info_t *disk = owner->current_disk();
    if (disk == nullptr || !disk->valid) {
        fl_message("Select a disk before creating a partition.");
        return false;
    }
    if (disk->free_part_idx < 0) {
        fl_message("No free partition slot is available on this disk.\n"
                   "Select a different disk or delete an existing partition first.");
        return false;
    }
    NewPartitionDialog dialog(owner);
    center_on(&dialog, owner);
    dialog.show();
    while (dialog.shown()) {
        Fl::wait();
    }
    if (!dialog.created()) {
        return false;
    }
    *slot = dialog.slot();
    return true;
}

void dialog_about(MainWindow *owner)
{
    AboutDialog dialog(owner);
    dialog.show();
    while (dialog.shown()) {
        Fl::wait();
    }
}

int dialog_new_image(MainWindow *owner)
{
    if (owner == nullptr) {
        return -1;
    }
    if (owner->state()->disk_count >= MAX_DISKS) {
        fl_message("The maximum number of open disks has been reached.");
        return -1;
    }
    NewImageDialog dialog(owner);
    center_on(&dialog, owner);
    dialog.show();
    while (dialog.shown()) {
        Fl::wait();
    }
    return dialog.index();
}
