#pragma once
// SoulMeter additions to Dear ImGui, used by the patched ImFont::RenderText()
// in imgui_draw.cpp. Included there without pch.h, so keep it self-contained.

#include <vector>
#include "imgui.h"

namespace ImGui {

    typedef struct _IMGUIOUTLINETEXT {
        ImU32 _color;
        float _size;
        // One copy offset down-right instead of four around the glyph.
        bool _shadow;
        _IMGUIOUTLINETEXT(ImU32 color, float size, bool shadow = false) : _color(color), _size(size), _shadow(shadow) {}
    }IMGUIOUTLINETEXT;

    class OutlineText {
    private:
        static std::vector<IMGUIOUTLINETEXT> _outlineColor;

    public:
        OutlineText() {}
        ~OutlineText() { _outlineColor.clear(); }

        static void PushOutlineText(IMGUIOUTLINETEXT info) { _outlineColor.push_back(info); }
        static void PopOutlineText() { _outlineColor.pop_back(); }
        static bool CheckOutlineTextFlag() { return !_outlineColor.empty(); }
        static IMGUIOUTLINETEXT* GetOutlineColor() {
            if (_outlineColor.empty())
                return nullptr;
            else
                return &*(_outlineColor.end() - 1);
        }
    };

    class TextAlignCenter {
    private:
        static bool _isSetAlignCenter;

    public:
        TextAlignCenter() {}
        ~TextAlignCenter() {}

        static void SetTextAlignCenter() { _isSetAlignCenter = true; }
        static void UnSetTextAlignCenter() { _isSetAlignCenter = false; }
        static bool CheckAlignCenter() { return _isSetAlignCenter; }
    };
}
