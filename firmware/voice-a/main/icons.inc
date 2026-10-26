struct MatrixIcon {
  const char* name;
  uint8_t rows[8];
};

static const struct MatrixIcon ICONS[] = {
  { "heart",       {0x66,0xFF,0xFF,0xFF,0x7E,0x3C,0x18,0x00} },
  { "smiley",      {0x3C,0x42,0xA5,0x81,0xA5,0x99,0x42,0x3C} },
  { "sad",         {0x3C,0x42,0xA5,0x81,0x99,0xA5,0x42,0x3C} },
  { "arrow_up",    {0x18,0x3C,0x7E,0xFF,0x18,0x18,0x18,0x18} },
  { "arrow_down",  {0x18,0x18,0x18,0x18,0xFF,0x7E,0x3C,0x18} },
  { "arrow_left",  {0x10,0x30,0x7F,0xFF,0x7F,0x30,0x10,0x00} },
  { "arrow_right", {0x08,0x0C,0xFE,0xFF,0xFE,0x0C,0x08,0x00} },
  { "check",       {0x00,0x01,0x03,0x86,0xCC,0x78,0x30,0x00} },
  { "cross",       {0xC3,0x66,0x3C,0x18,0x18,0x3C,0x66,0xC3} },
  { "star",        {0x18,0x18,0xFF,0x7E,0x3C,0x66,0xC3,0x00} },
  { "sun",         {0x18,0x99,0x5A,0x3C,0x3C,0x5A,0x99,0x18} },
  { "square",      {0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0xFF} },
  { "circle",      {0x3C,0x42,0x81,0x81,0x81,0x81,0x42,0x3C} },
  { "diamond",     {0x18,0x3C,0x7E,0xFF,0xFF,0x7E,0x3C,0x18} },
};

// Zet de gezette bits in de tekenbuffer; laat de rest van de buffer staan
// (matrix_clear() vooraf is aan de aanroeper).
static void drawRows(const uint8_t rows[8], uint8_t r, uint8_t g, uint8_t b) {
  for (int y = 0; y < 8; y++) {
    for (int x = 0; x < 8; x++) {
      if (rows[y] & (0x80 >> x)) {
        hal_matrix_set(x, y, r, g, b);
      }
    }
  }
}

void hal_matrix_bitmap(const uint8_t rows[8], uint8_t r, uint8_t g, uint8_t b) {
  drawRows(rows, r, g, b);
}

// 5x7-teken, gecentreerd (kolommen 1..5, rijen 0..6). Font: bit 0 = bovenste rij.
void hal_matrix_char(char c, uint8_t r, uint8_t g, uint8_t b) {
  if (c < 32 || c > 126) c = '?';
  const unsigned char* glyph = &FONT5X7[(c - 32) * 5];
  for (int col = 0; col < 5; col++) {
    for (int y = 0; y < 7; y++) {
      if (glyph[col] & (1 << y)) {
        hal_matrix_set(col + 1, y, r, g, b);
      }
    }
  }
}

int hal_matrix_icon(const char* name, uint8_t r, uint8_t g, uint8_t b) {
  for (size_t i = 0; i < sizeof(ICONS) / sizeof(ICONS[0]); i++) {
    if (strcasecmp(name, ICONS[i].name) == 0) {
      drawRows(ICONS[i].rows, r, g, b);
      return 1;
    }
  }
  hal_matrix_char('?', r, g, b);   // onbekende naam: vraagteken
  return 0;
}

int hal_abort_requested(void);   // gedefinieerd verderop

// Scrollt de tekst een keer voorbij (~60 ms per kolom) en showt zelf.
// De knop of 'x' breekt de scroll af.
void hal_matrix_text(const char* s, uint8_t r, uint8_t g, uint8_t b) {
  const int wtot = (int)strlen(s) * 6;
  for (int off = -8; off <= wtot; off++) {
    hal_matrix_fill(0,0,0);
    for (int x = 0; x < 8; x++) {
      const int vx = off + x;
      if (vx < 0 || vx >= wtot) continue;
      const int col = vx % 6;
      if (col == 5) continue;               // spatiekolom tussen tekens
      char c = s[vx / 6];
      if (c < 32 || c > 126) c = '?';
      const unsigned char bits = FONT5X7[(c - 32) * 5 + col];
      for (int y = 0; y < 7; y++) {
        if (bits & (1 << y)) hal_matrix_set(x, y, r, g, b);
      }
    }
    hal_matrix_show();
    hal_delay_ms(60);
    if (hal_abort_requested()) return;
  }
}

// Pulserende vier middelste pixels (rood) tijdens de opname; ~30 fps,
// 1,5 s per pulsslag. Bewust maar 4 pixels: zuinig op de voeding terwijl
// de wifi-zender vol staat te werken.
