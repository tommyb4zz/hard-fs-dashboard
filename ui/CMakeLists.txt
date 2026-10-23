<<<<<<< HEAD
SET(SOURCES components/ui_comp_label1.c
    components/ui_comp.c
    screens/ui_Screen1.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_frame_1_png.c
    fonts/ui_font_Mons110.c)
=======
# Purpose: Define the UI library source list, including current main/debug screens, component
# helpers and the selected generated image/font assets.

# Only these active sources enter the ui library. Legacy Screen1 and unused images/fonts
# remain in the repository but are excluded; consumers provide LVGL include/link settings.
# Adding Screen1 unchanged would collide with DebugScreen's ui_Image1 definition.
SET(SOURCES components/ui_comp_label1.c
    components/ui_comp.c
    screens/ui_MainScreen.c
    screens/ui_DebugScreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_rectangle_27_png.c
    images/ui_img_subtract_png.c
    images/ui_img_rectangle_24_png.c
    images/ui_img_debugframe_png.c
    fonts/ui_font_TomorrowBoldItalic12.c
    fonts/ui_font_TomorrowBoldItalic14.c
    fonts/ui_font_TomorrowBoldItalic18.c
    fonts/ui_font_TomorrowBoldItalic26.c
    fonts/ui_font_TomorrowBoldItalic64.c)
>>>>>>> yousef-dashboard

add_library(ui ${SOURCES})
