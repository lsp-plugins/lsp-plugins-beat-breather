/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-beat-breather
 * Created on: 8 дек. 2023 г.
 *
 * lsp-plugins-beat-breather is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-beat-breather is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-beat-breather. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef PRIVATE_UI_BEAT_BREATHER_H_
#define PRIVATE_UI_BEAT_BREATHER_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/lltl/darray.h>

namespace lsp
{
    namespace plugui
    {
        /**
         * UI for Beat breather plugin series
         */
        class beat_breather_ui: public ui::Module, public ui::IPortListener
        {
            protected:
                typedef struct split_t
                {
                    beat_breather_ui   *pUI;
                    ui::IPort          *pFreq;

                    tk::GraphMarker    *wMarker;        // Graph marker for editing
                    tk::GraphText      *wNote;          // Text with note and frequency
                } split_t;

            protected:
                lltl::darray<split_t> vSplits;          // List of split widgets and ports

            protected:

                static status_t slot_split_mouse_in(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_split_mouse_out(tk::Widget *sender, void *ptr, void *data);

            protected:

                template <class T>
                T              *find_split_widget(const char *fmt, const char *base, size_t id);
                ui::IPort      *find_port(const char *fmt, const char *base, size_t id);
                split_t        *find_split_by_widget(tk::Widget *widget);

            protected:
                void            on_split_mouse_in(split_t *s);
                void            on_split_mouse_out();

                void            add_splits();
                void            update_split_note_text(split_t *s);

            public:
                explicit beat_breather_ui(const meta::plugin_t *meta);
                virtual ~beat_breather_ui() override;

                virtual status_t    post_init() override;

                virtual void        notify(ui::IPort *port, size_t flags) override;
        };
    } // namespace plugui
} // namespace lsp

#endif /* PRIVATE_UI_BEAT_BREATHER_H_ */
