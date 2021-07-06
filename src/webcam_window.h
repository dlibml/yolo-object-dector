#ifndef webcam_window_h_INCLUDED
#define webcam_window_h_INCLUDED

#include <dlib/gui_widgets.h>
#include <dlib/image_transforms.h>
#include <tools/imglab/src/metadata_editor.h>

namespace dlib
{
    template <typename T, typename traits, typename alloc> auto string_dims(
        const std::basic_string<T, traits, alloc>& str,
        const std::shared_ptr<font>& f_ptr = default_font::get_font())
    {
        using string = std::basic_string<T, traits, alloc>;

        const font& f = *f_ptr;

        long height = f.height();
        long width = 0;
        for (typename string::size_type i = 0; i < str.size(); ++i)
        {
            // ignore the '\r' character
            if (str[i] == '\r')
                continue;

            // A combining character should be applied to the previous character, and we
            // therefore make one step back. If a combining comes right after a newline,
            // then there must be some kind of error in the string, and we don't combine.
            if (is_combining_char(str[i]))
            {
                width -= f[str[i]].width();
            }

            if (str[i] == '\n')
            {
                height += f.height();
                width = f.left_overflow();
                continue;
            }

            const letter& l = f[str[i]];
            width += l.width();
        }
        return std::make_pair(width, height);
    }
}  // namespace dlib

struct draw_options
{
    draw_options() = default;
    draw_options(const std::string& font_path)
    {
        if (not font_path.empty())
        {
            const auto font = std::make_shared<dlib::bdf_font>();
            std::ifstream fin(font_path);
            font->read_bdf_file(fin, 0xFFFF);
            font->adjust_metrics();
            custom_font = std::move(font);
        }
    };
    dlib::rgb_pixel font_color = dlib::rgb_pixel(0, 0, 0);
    size_t thickness = 5;
    color_mapper string_to_color;
    bool draw_labels = true;
    bool multilabel = false;
    const std::shared_ptr<dlib::font>& get_font()
    {
        if (custom_font != nullptr)
            font = custom_font;
        else
            font = default_font;
        return font;
    }

    private:
    const std::shared_ptr<dlib::font> default_font = dlib::default_font::get_font();
    std::shared_ptr<dlib::bdf_font> custom_font;
    std::shared_ptr<dlib::font> font;
};

inline void draw_bounding_boxes(
    dlib::matrix<dlib::rgb_pixel>& image,
    const std::vector<dlib::yolo_rect>& detections,
    draw_options& opts)
{
    // We want to draw most confident detections on top, so we iterate in reverse order
    for (auto d = detections.rbegin(); d != detections.rend(); ++d)
    {
        const auto offset = opts.thickness / 2;
        const auto color = opts.string_to_color(d->label);
        dlib::rectangle r(d->rect);
        r.left() = dlib::put_in_range(offset, image.nc() - 1 - offset, r.left());
        r.top() = dlib::put_in_range(offset, image.nr() - 1 - offset, r.top());
        r.right() = dlib::put_in_range(offset, image.nc() - 1 - offset, r.right());
        r.bottom() = dlib::put_in_range(offset, image.nr() - 1 - offset, r.bottom());
        dlib::draw_rectangle(image, r, color, opts.thickness);

        if (opts.draw_labels)
        {
            std::ostringstream sout;
            sout << std::fixed << std::setprecision(0);
            if (opts.multilabel)
            {
                for (size_t i = 0; i < d->labels.size() - 1; ++i)
                    sout << d->labels[i].second << " (" << d->labels[i].first * 100 << "%), ";
                sout << d->labels[d->labels.size() - 1].second << " ("
                     << d->labels[d->labels.size() - 1].first * 100 << "%)";
            }
            else
            {
                sout << d->label << " (" << d->detection_confidence * 100 << "%)";
            }

            const dlib::ustring label = dlib::convert_utf8_to_utf32(sout.str());
            const auto [lw, lh] = string_dims(label, opts.get_font());

            // the default case: label outside the top left corner of the box
            dlib::point label_pos(r.left(), r.top() - lh);
            dlib::rectangle bg(lw + opts.thickness, lh);

            // draw label inside the bounding box (move it downwards)
            if (label_pos.y() < 0)
                label_pos = dlib::point(r.left(), r.top());

            bg = move_rect(bg, label_pos.x() - offset, label_pos.y());
            fill_rect(image, bg, color);
            draw_string(image, dlib::point(label_pos), label, opts.font_color, opts.get_font());
        }
    }
}

class webcam_window : public dlib::image_window
{
    public:
    webcam_window() { update_title(); };
    explicit webcam_window(const double conf_thresh) : conf_thresh(conf_thresh)
    {
        update_title();
        create_recording_icon();
    }
    bool mirror = true;
    float conf_thresh = 0.25;
    bool recording = false;
    bool can_record = false;

    static void print_keyboard_shortcuts()
    {
        std::cout << "Keyboard Shortcuts:" << std::endl;
        std::cout << "  h                     display keyboard shortcuts\n";
        std::cout << "  m                     toggle mirror mode\n";
        std::cout << "  +, k                  increase confidence threshold by 0.01\n";
        std::cout << "  -, j                  decrease confidence threshold by 0.01\n";
        std::cout << "  r                     toggle recording (needs --output option)\n";
        std::cout << "  q                     quit the application\n";
        std::cout << std::endl;
    }

    void show_recording_icon() { add_overlay(recording_icon); }

    private:
    std::vector<overlay_circle> recording_icon;
    void update_title()
    {
        std::ostringstream sout;
        sout << "YOLO @" << std::setprecision(2) << std::fixed << conf_thresh;
        set_title(sout.str());
    }
    void create_recording_icon()
    {
        const auto c = dlib::point(20, 20);
        const auto color = dlib::rgb_pixel(255, 0, 0);
        for (auto r = 0.0; r < 10; r += 0.1)
        {
            recording_icon.emplace_back(c, r, color);
        }
    }
    void on_keydown(unsigned long key, bool /*is_printable*/, unsigned long /*state*/) override
    {
        switch (key)
        {
        case 'h':
            print_keyboard_shortcuts();
            break;
        case 'm':
            mirror = !mirror;
            break;
        case '+':
        case 'k':
            conf_thresh = std::min(conf_thresh + 0.01f, 1.0f);
            update_title();
            break;
        case '-':
        case 'j':
            conf_thresh = std::max(conf_thresh - 0.01f, 0.01f);
            update_title();
            break;
        case 'r':
            if (not can_record)
                break;
            recording = !recording;
            if (recording)
                show_recording_icon();
            else
                clear_overlay();
            break;
        case 'q':
            close_window();
            break;
        default:
            break;
        }
    }
};

#endif  // webcam_window_h_INCLUDED
