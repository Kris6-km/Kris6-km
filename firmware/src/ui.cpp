#include "ui.h"
#include "display.h"
#include "modbus_scanner.h"
#include "can_scanner.h"
#include "settings.h"
#include "storage.h"
#include "config.h"
#include "webui.h"
#include <lvgl.h>

// ----------------------- UI state -----------------------
extern ScannerSettings g_settings;

static lv_obj_t *scr_main = nullptr;
static lv_obj_t *tabview  = nullptr;

// Dashboard
static lv_obj_t *lbl_status;
static lv_obj_t *lbl_wifi;
static lv_obj_t *bar_progress;
static lv_obj_t *lbl_progress;
static lv_obj_t *lbl_found;
static lv_obj_t *list_devices;

// Settings
static lv_obj_t *dd_baud;
static lv_obj_t *dd_parity;
static lv_obj_t *dd_stop;
static lv_obj_t *sw_autobaud;
static lv_obj_t *sw_logsd;
static lv_obj_t *ta_scan_first;
static lv_obj_t *ta_scan_last;

// Register read
static lv_obj_t *ta_slave_id;
static lv_obj_t *dd_regtype;
static lv_obj_t *ta_reg_start;
static lv_obj_t *ta_reg_count;
static lv_obj_t *tbl_regs;

extern std::vector<ModbusDevice> g_last_scan;

// CAN state
static lv_obj_t *dd_can_bitrate;
static lv_obj_t *sw_can_autobaud;
static lv_obj_t *ta_can_dur;
static lv_obj_t *lbl_can_status;
static lv_obj_t *bar_can;
static lv_obj_t *tbl_can;
static std::vector<CanNode> g_last_can;
static const char *can_bitrate_opts   = "50000\n100000\n125000\n250000\n500000\n1000000";
static const uint32_t can_bitrate_vals[] = {50000, 100000, 125000, 250000, 500000, 1000000};

// ----------------------- helpers ------------------------
static const char *baud_options  = "4800\n9600\n19200\n38400\n57600\n115200";
static const uint32_t baud_values[] = {4800, 9600, 19200, 38400, 57600, 115200};
static const char *parity_options = "None\nEven\nOdd";
static const char *stop_options   = "1\n2";
static const char *regtype_options = "Holding (0x03)\nInput (0x04)\nCoil (0x01)\nDiscrete (0x02)";

static int baudToIndex(uint32_t b) {
    for (int i = 0; i < (int)(sizeof(baud_values)/sizeof(uint32_t)); ++i)
        if (baud_values[i] == b) return i;
    return 1;
}

static int atoi_safe(lv_obj_t *ta, int defVal) {
    const char *t = lv_textarea_get_text(ta);
    if (!t || !*t) return defVal;
    int v = atoi(t);
    return v;
}

static void append_log_line(const String &line) {
    log_i("%s", line.c_str());
    if (g_settings.logToSd && storage_sd_available()) {
        storage_log_event(line);
    }
}

// ----------------------- scan task ----------------------
static void scan_task(void *) {
    g_last_scan.clear();
    lv_async_call([](void*){
        display_lock();
        lv_obj_clean(list_devices);
        lv_label_set_text(lbl_status, "Skanowanie...");
        lv_bar_set_value(bar_progress, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_progress, "0 %");
        lv_label_set_text(lbl_found, "Znaleziono: 0");
        display_unlock();
    }, nullptr);

    auto progress_cb = [](uint8_t cur, uint8_t first, uint8_t last, int found) {
        int pct = 0;
        if (last > first) pct = (int)((cur - first) * 100 / (last - first));
        ui_set_scan_progress(cur, first, last, found);
        (void)pct;
    };

    uint32_t fb = g_settings.baudrate;
    uint8_t  fp = g_settings.parity;
    uint8_t  fs = g_settings.stopBits;

    if (g_settings.autoBaud) {
        g_last_scan = Scanner.scanAutoBaud(g_settings.scanFirst, g_settings.scanLast,
                                            fb, fp, fs, progress_cb);
        g_settings.baudrate = fb;
        g_settings.parity = fp;
        g_settings.stopBits = fs;
        settings_save(g_settings);
    } else {
        Scanner.applySerial(g_settings.baudrate, g_settings.parity, g_settings.stopBits);
        g_last_scan = Scanner.scanRange(g_settings.scanFirst, g_settings.scanLast, progress_cb);
    }

    append_log_line(String("scan_done,count=") + g_last_scan.size() +
                    ",baud=" + fb + ",parity=" + fp + ",stop=" + fs);

    // Build CSV for SD
    if (g_settings.logToSd && storage_sd_available()) {
        String csv = "id,baud,parity,stop\n";
        for (auto &d : g_last_scan) {
            csv += String(d.id) + "," + d.baud + "," + d.parity + "," + d.stop + "\n";
        }
        storage_write_scan_csv(csv);
    }

    ui_set_scan_result((int)g_last_scan.size());
    vTaskDelete(nullptr);
}

// ----------------------- UI callbacks -------------------
static void on_scan_btn(lv_event_t *) {
    if (Scanner.busy()) return;
    xTaskCreatePinnedToCore(scan_task, "scan", 6144, nullptr, 1, nullptr, 1);
}
static void on_stop_btn(lv_event_t *) { Scanner.abort(); }

static void on_save_settings(lv_event_t *) {
    g_settings.baudrate = baud_values[lv_dropdown_get_selected(dd_baud)];
    g_settings.parity   = lv_dropdown_get_selected(dd_parity);
    g_settings.stopBits = lv_dropdown_get_selected(dd_stop) == 1 ? 2 : 1;
    g_settings.autoBaud = lv_obj_has_state(sw_autobaud, LV_STATE_CHECKED);
    g_settings.logToSd  = lv_obj_has_state(sw_logsd, LV_STATE_CHECKED);
    g_settings.scanFirst = constrain(atoi_safe(ta_scan_first, 1),   1, 247);
    g_settings.scanLast  = constrain(atoi_safe(ta_scan_last,  247), 1, 247);
    settings_save(g_settings);
    Scanner.applySerial(g_settings.baudrate, g_settings.parity, g_settings.stopBits);
    lv_label_set_text(lbl_status, "Zapisano ustawienia.");
}

static void on_read_regs(lv_event_t *) {
    uint8_t id = constrain(atoi_safe(ta_slave_id, 1), 1, 247);
    uint16_t start = constrain(atoi_safe(ta_reg_start, 0), 0, 65535);
    uint16_t count = constrain(atoi_safe(ta_reg_count, 10), 1, 125);
    int sel = lv_dropdown_get_selected(dd_regtype);
    RegType rt = REG_HOLDING;
    switch (sel) { case 0: rt=REG_HOLDING; break; case 1: rt=REG_INPUT; break;
                   case 2: rt=REG_COIL; break; case 3: rt=REG_DISCRETE; break; }

    auto rv = Scanner.readRegisters(id, rt, start, count);
    lv_table_set_row_cnt(tbl_regs, rv.size() + 1);
    lv_table_set_col_cnt(tbl_regs, 3);
    lv_table_set_cell_value(tbl_regs, 0, 0, "Addr");
    lv_table_set_cell_value(tbl_regs, 0, 1, "Dec");
    lv_table_set_cell_value(tbl_regs, 0, 2, "Hex");
    for (size_t i = 0; i < rv.size(); ++i) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", rv[i].address);
        lv_table_set_cell_value(tbl_regs, i+1, 0, buf);
        if (rv[i].ok) {
            snprintf(buf, sizeof(buf), "%u", rv[i].value);
            lv_table_set_cell_value(tbl_regs, i+1, 1, buf);
            snprintf(buf, sizeof(buf), "0x%04X", rv[i].value);
            lv_table_set_cell_value(tbl_regs, i+1, 2, buf);
        } else {
            lv_table_set_cell_value(tbl_regs, i+1, 1, "err");
            lv_table_set_cell_value(tbl_regs, i+1, 2, Scanner.lastError().c_str());
        }
    }
}

static void on_device_clicked(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_list_get_btn_text(list_devices, btn);
    if (!txt) return;
    // "ID 7 @ 9600 8N1"
    int id = 0;
    if (sscanf(txt, "ID %d", &id) == 1 && id > 0 && id < 248) {
        char buf[8]; snprintf(buf, sizeof(buf), "%d", id);
        lv_textarea_set_text(ta_slave_id, buf);
        lv_tabview_set_act(tabview, 2, LV_ANIM_ON);
    }
}

// ----------------------- UI build -----------------------
static void build_tab_scan(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_gap(tab, 10, 0);

    lbl_status = lv_label_create(tab);
    lv_label_set_text(lbl_status, "Gotowy.");
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xE6EDF3), 0);

    lbl_wifi = lv_label_create(tab);
    lv_label_set_text(lbl_wifi, "WiFi: ...");
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x7DD3FC), 0);

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(row, 8, 0);

    lv_obj_t *btn_scan = lv_btn_create(row);
    lv_obj_set_height(btn_scan, 48);
    lv_obj_set_flex_grow(btn_scan, 1);
    lv_obj_add_event_cb(btn_scan, on_scan_btn, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l1 = lv_label_create(btn_scan); lv_label_set_text(l1, LV_SYMBOL_REFRESH " Skanuj");
    lv_obj_center(l1);

    lv_obj_t *btn_stop = lv_btn_create(row);
    lv_obj_set_height(btn_stop, 48);
    lv_obj_set_flex_grow(btn_stop, 1);
    lv_obj_set_style_bg_color(btn_stop, lv_color_hex(0xDC2626), 0);
    lv_obj_add_event_cb(btn_stop, on_stop_btn, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l2 = lv_label_create(btn_stop); lv_label_set_text(l2, LV_SYMBOL_STOP " Stop");
    lv_obj_center(l2);

    bar_progress = lv_bar_create(tab);
    lv_obj_set_width(bar_progress, LV_PCT(100));
    lv_bar_set_range(bar_progress, 0, 100);

    lbl_progress = lv_label_create(tab);
    lv_label_set_text(lbl_progress, "0 %");

    lbl_found = lv_label_create(tab);
    lv_label_set_text(lbl_found, "Znaleziono: 0");

    list_devices = lv_list_create(tab);
    lv_obj_set_width(list_devices, LV_PCT(100));
    lv_obj_set_flex_grow(list_devices, 1);
}

static void build_tab_settings(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_gap(tab, 10, 0);

    auto makeLabel = [&](const char *txt) {
        lv_obj_t *l = lv_label_create(tab);
        lv_label_set_text(l, txt);
        return l;
    };

    makeLabel("Predkosc (baud)");
    dd_baud = lv_dropdown_create(tab);
    lv_dropdown_set_options(dd_baud, baud_options);
    lv_dropdown_set_selected(dd_baud, baudToIndex(g_settings.baudrate));

    makeLabel("Parzystosc");
    dd_parity = lv_dropdown_create(tab);
    lv_dropdown_set_options(dd_parity, parity_options);
    lv_dropdown_set_selected(dd_parity, g_settings.parity);

    makeLabel("Bity stopu");
    dd_stop = lv_dropdown_create(tab);
    lv_dropdown_set_options(dd_stop, stop_options);
    lv_dropdown_set_selected(dd_stop, g_settings.stopBits == 2 ? 1 : 0);

    lv_obj_t *row1 = lv_obj_create(tab); lv_obj_remove_style_all(row1);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_gap(row1, 8, 0);
    lv_obj_set_width(row1, LV_PCT(100));
    sw_autobaud = lv_switch_create(row1);
    if (g_settings.autoBaud) lv_obj_add_state(sw_autobaud, LV_STATE_CHECKED);
    lv_obj_t *l1 = lv_label_create(row1); lv_label_set_text(l1, "Auto-detekcja predkosci");

    lv_obj_t *row2 = lv_obj_create(tab); lv_obj_remove_style_all(row2);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_gap(row2, 8, 0);
    lv_obj_set_width(row2, LV_PCT(100));
    sw_logsd = lv_switch_create(row2);
    if (g_settings.logToSd) lv_obj_add_state(sw_logsd, LV_STATE_CHECKED);
    lv_obj_t *l2 = lv_label_create(row2); lv_label_set_text(l2, "Logowanie na karte SD");

    makeLabel("Zakres skanowania (od - do)");
    lv_obj_t *rowR = lv_obj_create(tab); lv_obj_remove_style_all(rowR);
    lv_obj_set_flex_flow(rowR, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_gap(rowR, 8, 0);
    lv_obj_set_width(rowR, LV_PCT(100));
    ta_scan_first = lv_textarea_create(rowR);
    lv_textarea_set_one_line(ta_scan_first, true);
    lv_textarea_set_accepted_chars(ta_scan_first, "0123456789");
    char bb[8]; snprintf(bb, sizeof(bb), "%u", g_settings.scanFirst);
    lv_textarea_set_text(ta_scan_first, bb);
    lv_obj_set_flex_grow(ta_scan_first, 1);
    ta_scan_last = lv_textarea_create(rowR);
    lv_textarea_set_one_line(ta_scan_last, true);
    lv_textarea_set_accepted_chars(ta_scan_last, "0123456789");
    snprintf(bb, sizeof(bb), "%u", g_settings.scanLast);
    lv_textarea_set_text(ta_scan_last, bb);
    lv_obj_set_flex_grow(ta_scan_last, 1);

    lv_obj_t *btn_save = lv_btn_create(tab);
    lv_obj_set_height(btn_save, 48);
    lv_obj_add_event_cb(btn_save, on_save_settings, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbs = lv_label_create(btn_save); lv_label_set_text(lbs, LV_SYMBOL_SAVE " Zapisz");
    lv_obj_center(lbs);
}

static void build_tab_regs(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_gap(tab, 8, 0);

    lv_obj_t *row = lv_obj_create(tab); lv_obj_remove_style_all(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_gap(row, 6, 0);
    lv_obj_set_width(row, LV_PCT(100));

    ta_slave_id = lv_textarea_create(row);
    lv_textarea_set_one_line(ta_slave_id, true);
    lv_textarea_set_placeholder_text(ta_slave_id, "Slave ID");
    lv_textarea_set_accepted_chars(ta_slave_id, "0123456789");
    lv_textarea_set_text(ta_slave_id, "1");
    lv_obj_set_width(ta_slave_id, 90);

    dd_regtype = lv_dropdown_create(row);
    lv_dropdown_set_options(dd_regtype, regtype_options);
    lv_obj_set_flex_grow(dd_regtype, 1);

    lv_obj_t *row2 = lv_obj_create(tab); lv_obj_remove_style_all(row2);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_gap(row2, 6, 0);
    lv_obj_set_width(row2, LV_PCT(100));
    ta_reg_start = lv_textarea_create(row2);
    lv_textarea_set_one_line(ta_reg_start, true);
    lv_textarea_set_placeholder_text(ta_reg_start, "Start");
    lv_textarea_set_accepted_chars(ta_reg_start, "0123456789");
    lv_textarea_set_text(ta_reg_start, "0");
    lv_obj_set_flex_grow(ta_reg_start, 1);
    ta_reg_count = lv_textarea_create(row2);
    lv_textarea_set_one_line(ta_reg_count, true);
    lv_textarea_set_placeholder_text(ta_reg_count, "Count");
    lv_textarea_set_accepted_chars(ta_reg_count, "0123456789");
    lv_textarea_set_text(ta_reg_count, "10");
    lv_obj_set_flex_grow(ta_reg_count, 1);
    lv_obj_t *btn_read = lv_btn_create(row2);
    lv_obj_add_event_cb(btn_read, on_read_regs, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lr = lv_label_create(btn_read); lv_label_set_text(lr, LV_SYMBOL_DOWNLOAD " Czytaj");

    tbl_regs = lv_table_create(tab);
    lv_obj_set_width(tbl_regs, LV_PCT(100));
    lv_obj_set_flex_grow(tbl_regs, 1);
    lv_table_set_col_cnt(tbl_regs, 3);
    lv_table_set_cell_value(tbl_regs, 0, 0, "Addr");
    lv_table_set_cell_value(tbl_regs, 0, 1, "Dec");
    lv_table_set_cell_value(tbl_regs, 0, 2, "Hex");
}

// ----------------------- CAN scan task -----------------
static void can_scan_task(void *) {
    g_last_can.clear();
    lv_async_call([](void*){
        display_lock();
        lv_table_set_row_cnt(tbl_can, 1);
        lv_bar_set_value(bar_can, 0, LV_ANIM_OFF);
        lv_label_set_text(lbl_can_status, "Nasluch CAN...");
        display_unlock();
    }, nullptr);

    uint32_t bitrate = g_settings.canBitrate;
    if (g_settings.canAutoBaud) {
        bitrate = CanScan.autoDetectBitrate(1200);
        if (bitrate == 0) {
            lv_async_call([](void*){
                lv_label_set_text(lbl_can_status, "Nie wykryto ruchu CAN.");
            }, nullptr);
            vTaskDelete(nullptr);
            return;
        }
        g_settings.canBitrate = bitrate;
        settings_save(g_settings);
    } else {
        CanScan.setMode(true);  // ensure listen-only
        if (!CanScan.begin(bitrate)) {
            String err = CanScan.lastError();
            char *copy = strdup(err.c_str());
            lv_async_call([](void *v){
                lv_label_set_text_fmt(lbl_can_status, "Blad: %s", (const char*)v);
                free(v);
            }, copy);
            vTaskDelete(nullptr);
            return;
        }
    }

    g_last_can = CanScan.scanPassive(g_settings.canScanMs,
        [](uint32_t el, uint32_t tot, size_t ids, uint32_t frames) {
            struct Msg { uint32_t el, tot; size_t ids; uint32_t fr; };
            auto *m = new Msg{ el, tot, ids, frames };
            lv_async_call([](void *v){
                auto *mm = (Msg*)v;
                int pct = mm->tot ? (int)(mm->el * 100 / mm->tot) : 0;
                lv_bar_set_value(bar_can, pct, LV_ANIM_OFF);
                lv_label_set_text_fmt(lbl_can_status,
                    "Nasluch %u/%u ms  |  %u ID  |  %u ramek",
                    mm->el, mm->tot, (unsigned)mm->ids, mm->fr);
                delete mm;
            }, m);
        });

    CanScan.end();

    lv_async_call([](void*){
        lv_table_set_col_cnt(tbl_can, 4);
        lv_table_set_cell_value(tbl_can, 0, 0, "ID");
        lv_table_set_cell_value(tbl_can, 0, 1, "Typ");
        lv_table_set_cell_value(tbl_can, 0, 2, "DLC");
        lv_table_set_cell_value(tbl_can, 0, 3, "Ramki");
        lv_table_set_row_cnt(tbl_can, g_last_can.size() + 1);
        char buf[24];
        for (size_t i = 0; i < g_last_can.size(); ++i) {
            auto &n = g_last_can[i];
            snprintf(buf, sizeof(buf), n.extended ? "0x%08X" : "0x%03X", n.id);
            lv_table_set_cell_value(tbl_can, i+1, 0, buf);
            lv_table_set_cell_value(tbl_can, i+1, 1, n.extended ? "29" : "11");
            snprintf(buf, sizeof(buf), "%u", n.dlc);
            lv_table_set_cell_value(tbl_can, i+1, 2, buf);
            snprintf(buf, sizeof(buf), "%u", n.frameCount);
            lv_table_set_cell_value(tbl_can, i+1, 3, buf);
        }
        lv_label_set_text_fmt(lbl_can_status, "Zakonczono. Unikalne ID: %u",
            (unsigned)g_last_can.size());
    }, nullptr);

    if (g_settings.logToSd && storage_sd_available() && !g_last_can.empty()) {
        String csv = "id,type,dlc,frames\n";
        char buf[32];
        for (auto &n : g_last_can) {
            snprintf(buf, sizeof(buf), n.extended ? "0x%08X,29," : "0x%03X,11,", n.id);
            csv += buf;
            csv += String(n.dlc) + "," + String(n.frameCount) + "\n";
        }
        storage_write_scan_csv(csv);
    }
    vTaskDelete(nullptr);
}

static void on_can_scan_btn(lv_event_t *) {
    g_settings.canBitrate = can_bitrate_vals[lv_dropdown_get_selected(dd_can_bitrate)];
    g_settings.canAutoBaud = lv_obj_has_state(sw_can_autobaud, LV_STATE_CHECKED);
    g_settings.canScanMs   = constrain(atoi_safe(ta_can_dur, 10000), 500, 300000);
    settings_save(g_settings);
    xTaskCreatePinnedToCore(can_scan_task, "can_scan", 6144, nullptr, 1, nullptr, 1);
}

static void build_tab_can(lv_obj_t *tab) {
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(tab, 12, 0);
    lv_obj_set_style_pad_gap(tab, 10, 0);

    lv_obj_t *l1 = lv_label_create(tab);
    lv_label_set_text(l1, "Predkosc CAN (bps)");
    dd_can_bitrate = lv_dropdown_create(tab);
    lv_dropdown_set_options(dd_can_bitrate, can_bitrate_opts);
    // Set current selection
    int sel = 4; // 500k default
    for (int i = 0; i < (int)(sizeof(can_bitrate_vals)/sizeof(uint32_t)); ++i)
        if (can_bitrate_vals[i] == g_settings.canBitrate) { sel = i; break; }
    lv_dropdown_set_selected(dd_can_bitrate, sel);

    lv_obj_t *row = lv_obj_create(tab); lv_obj_remove_style_all(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_gap(row, 8, 0);
    lv_obj_set_width(row, LV_PCT(100));
    sw_can_autobaud = lv_switch_create(row);
    if (g_settings.canAutoBaud) lv_obj_add_state(sw_can_autobaud, LV_STATE_CHECKED);
    lv_obj_t *lsw = lv_label_create(row); lv_label_set_text(lsw, "Auto-detekcja predkosci");

    lv_obj_t *l2 = lv_label_create(tab);
    lv_label_set_text(l2, "Czas nasluchu [ms]");
    ta_can_dur = lv_textarea_create(tab);
    lv_textarea_set_one_line(ta_can_dur, true);
    lv_textarea_set_accepted_chars(ta_can_dur, "0123456789");
    char dbuf[16]; snprintf(dbuf, sizeof(dbuf), "%u", g_settings.canScanMs);
    lv_textarea_set_text(ta_can_dur, dbuf);

    lv_obj_t *btn = lv_btn_create(tab);
    lv_obj_set_height(btn, 48);
    lv_obj_add_event_cb(btn, on_can_scan_btn, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, LV_SYMBOL_REFRESH " Nasluchuj CAN");
    lv_obj_center(lb);

    bar_can = lv_bar_create(tab);
    lv_obj_set_width(bar_can, LV_PCT(100));
    lv_bar_set_range(bar_can, 0, 100);

    lbl_can_status = lv_label_create(tab);
    lv_label_set_text(lbl_can_status, "Gotowy do nasluchu CAN.");

    tbl_can = lv_table_create(tab);
    lv_obj_set_width(tbl_can, LV_PCT(100));
    lv_obj_set_flex_grow(tbl_can, 1);
    lv_table_set_col_cnt(tbl_can, 4);
    lv_table_set_cell_value(tbl_can, 0, 0, "ID");
    lv_table_set_cell_value(tbl_can, 0, 1, "Typ");
    lv_table_set_cell_value(tbl_can, 0, 2, "DLC");
    lv_table_set_cell_value(tbl_can, 0, 3, "Ramki");
}

void ui_init() {
    display_lock();

    // Dark theme palette
    lv_disp_t *disp = lv_disp_get_default();
    lv_theme_t *th = lv_theme_default_init(disp,
        lv_color_hex(0x3B82F6), lv_color_hex(0xF59E0B), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(disp, th);

    scr_main = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(0x0B1220), 0);
    lv_scr_load(scr_main);

    tabview = lv_tabview_create(scr_main, LV_DIR_TOP, 50);
    lv_obj_set_size(tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_t *tab1 = lv_tabview_add_tab(tabview, LV_SYMBOL_WIFI "  Skaner");
    lv_obj_t *tab2 = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS "  Ustawienia");
    lv_obj_t *tab3 = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST "  Rejestry");
    lv_obj_t *tab4 = lv_tabview_add_tab(tabview, LV_SYMBOL_SHUFFLE "  CAN");

    build_tab_scan(tab1);
    build_tab_settings(tab2);
    build_tab_regs(tab3);
    build_tab_can(tab4);

    // make list items clickable to jump to register-read tab
    lv_obj_add_event_cb(list_devices, on_device_clicked, LV_EVENT_CLICKED, nullptr);

    display_unlock();
}

void ui_tick() {}

// ---------- thread-safe updates from scanner task ----------
struct ProgressMsg { uint8_t cur, first, last; int found; };

static void progress_apply(void *v) {
    auto *m = (ProgressMsg*)v;
    int pct = 0;
    if (m->last > m->first) pct = (int)((m->cur - m->first) * 100 / (m->last - m->first));
    lv_bar_set_value(bar_progress, pct, LV_ANIM_OFF);
    char buf[64]; snprintf(buf, sizeof(buf), "ID %u/%u  (%d %%)", m->cur, m->last, pct);
    lv_label_set_text(lbl_progress, buf);
    snprintf(buf, sizeof(buf), "Znaleziono: %d", m->found);
    lv_label_set_text(lbl_found, buf);
    delete m;
}

void ui_set_scan_progress(uint8_t id, uint8_t first, uint8_t last, int found) {
    auto *m = new ProgressMsg{ id, first, last, found };
    lv_async_call(progress_apply, m);
}

static void result_apply(void *v) {
    int count = (int)(intptr_t)v;
    lv_obj_clean(list_devices);
    extern std::vector<ModbusDevice> g_last_scan;
    for (auto &d : g_last_scan) {
        char line[64];
        const char *par = (d.parity == 0 ? "N" : (d.parity == 1 ? "E" : "O"));
        snprintf(line, sizeof(line), "ID %u @ %u 8%s%u", d.id, d.baud, par, d.stop);
        lv_obj_t *btn = lv_list_add_btn(list_devices, LV_SYMBOL_FILE, line);
        lv_obj_add_event_cb(btn, on_device_clicked, LV_EVENT_CLICKED, nullptr);
    }
    lv_bar_set_value(bar_progress, 100, LV_ANIM_ON);
    char buf[64]; snprintf(buf, sizeof(buf), "Zakonczono. Znaleziono: %d", count);
    lv_label_set_text(lbl_status, buf);
}

void ui_set_scan_result(int count) {
    lv_async_call(result_apply, (void*)(intptr_t)count);
}

void ui_set_status(const String &text) {
    char *copy = strdup(text.c_str());
    lv_async_call([](void *v){
        lv_label_set_text(lbl_status, (const char*)v);
        free(v);
    }, copy);
}

void ui_set_wifi_info(const String &info) {
    char *copy = strdup(info.c_str());
    lv_async_call([](void *v){
        lv_label_set_text(lbl_wifi, (const char*)v);
        free(v);
    }, copy);
}
