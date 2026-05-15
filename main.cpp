// TraceShadow - console-based light/shadow exploration game
// Implements concept.txt: keyboard movement, map.txt-based map, light/shadow effect.

#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

static const int MAX_W = 256;
static const int MAX_H = 128;
static const int LIGHT_RADIUS = 7;

static char  g_map[MAX_H][MAX_W];
static bool  g_visible[MAX_H][MAX_W];
static int   g_W = 0;
static int   g_H = 0;
static int   g_px = 0;
static int   g_py = 0;

static HANDLE g_hOut = NULL;

static bool load_map(const char* path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(ifs, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    if (lines.empty()) return false;

    int maxw = 0;
    for (const auto& l : lines)
    {
        if ((int)l.size() > maxw) maxw = (int)l.size();
    }
    if (maxw <= 0) return false;
    if (maxw  >= MAX_W) maxw = MAX_W - 1;
    int h = (int)lines.size();
    if (h >= MAX_H) h = MAX_H - 1;

    g_W = maxw;
    g_H = h;
    for (int y = 0; y < g_H; ++y)
    {
        const std::string& l = lines[y];
        for (int x = 0; x < g_W; ++x)
        {
            char c = (x < (int)l.size()) ? l[x] : ' ';
            g_map[y][x] = c;
        }
        g_map[y][g_W] = '\0';
    }
    return true;
}

static bool find_start(int& sx, int& sy)
{
    for (int y = 0; y < g_H; ++y)
    {
        for (int x = 0; x < g_W; ++x)
        {
            if (g_map[y][x] == ' ')
            {
                sx = x;
                sy = y;
                return true;
            }
        }
    }
    return false;
}

static inline bool blocks_light(char c)
{
    return c == '#' || c == '*';
}

static inline bool blocks_move(char c)
{
    return c == '#' || c == '*';
}

// Recursive shadowcasting (Björn Bergström). Processes one octant per call.
// Transform table maps (row, col) in the canonical octant to world deltas.
static const int OCTANT_XFORM[8][4] = 
{
    { 1,  0,  0,  1},
    { 0,  1,  1,  0},
    { 0, -1,  1,  0},
    {-1,  0,  0,  1},
    {-1,  0,  0, -1},
    { 0, -1, -1,  0},
    { 0,  1, -1,  0},
    { 1,  0,  0, -1},
};

static void cast_light(int row, double start_slope, double end_slope, int radius, int xx, int xy, int yx, int yy)
{
    if (start_slope < end_slope) return;

    const int R2 = radius * radius;
    double next_start = start_slope;
    bool prev_blocked = false;

    for (int dist = row; dist <= radius; ++dist)
    {
        int dy = -dist;
        bool any_in_bounds = false;

        // 가로 방향 조사
        for (int dx = -dist; dx <= 0; ++dx)
        {
            int cx = g_px + dx * xx + dy * xy;
            int cy = g_py + dx * yx + dy * yy;

            // Slopes of this cell's left and right edges (octant-local).
            double left_slope  = (dx - 0.5) / (dy + 0.5);
            double right_slope = (dx + 0.5) / (dy - 0.5);

            if (start_slope < right_slope)
            {
                continue;
            }
            if (end_slope > left_slope)
            {
                break;
            }

            bool in_bounds = (cx >= 0 && cx < g_W && cy >= 0 && cy < g_H);
            if (in_bounds)
            {
                any_in_bounds = true;
                if (dx * dx + dy * dy <= R2)
                {
                    g_visible[cy][cx] = true;
                }
            }

            bool blocks = in_bounds && blocks_light(g_map[cy][cx]);

            if (prev_blocked)
            {
                if (blocks)
                {
                    next_start = right_slope;
                }
                else
                {
                    prev_blocked = false;
                    start_slope = next_start;
                }
            }
            else
            {
                if (blocks && dist < radius)
                {
                    prev_blocked = true;
                    cast_light(dist + 1, start_slope, left_slope, radius,
                               xx, xy, yx, yy);
                    next_start = right_slope;
                }
            }
        }

        // If the entire row was out of bounds and we're still scanning, stop.
        if (!any_in_bounds) break;
        if (prev_blocked) break;
    }
}

static void compute_light()
{
    for (int y = 0; y < g_H; ++y)
    {
        for (int x = 0; x < g_W; ++x)
        {
            g_visible[y][x] = false;
        }
    }
    g_visible[g_py][g_px] = true;

    for (int oct = 0; oct < 8; ++oct)
    {
        cast_light(1, 1.0, 0.0, LIGHT_RADIUS, OCTANT_XFORM[oct][0], OCTANT_XFORM[oct][1], OCTANT_XFORM[oct][2], OCTANT_XFORM[oct][3]);
    }
}

static void hide_cursor()
{
    CONSOLE_CURSOR_INFO ci;
    ci.dwSize = 1;
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &ci);
}

static void move_cursor_home()
{
    COORD c = {0, 0};
    SetConsoleCursorPosition(g_hOut, c);
}

static void render()
{
    // Build the whole frame into a buffer then write once (no flicker).
    std::string buf;
    buf.reserve((g_W + 2) * (g_H + 4));

    buf += "TraceShadow  -  WASD/Arrows: Move,  ESC: Quit\n";
    char info[128];
    std::snprintf(info, sizeof(info), "Player: (%d, %d)   Light radius: %d\n",
                  g_px, g_py, LIGHT_RADIUS);
    buf += info;
    buf += '\n';

    for (int y = 0; y < g_H; ++y)
    {
        for (int x = 0; x < g_W; ++x)
        {
            char out;
            if (x == g_px && y == g_py)
            {
                out = '@';
            }
            else if (!g_visible[y][x])
            {
                out = ' ';
            }
            else
            {
                char m = g_map[y][x];
                if (m == ' ')
                {
                    out = '.';
                }
                else
                {
                    out = m;  // '#' or '*' shown as-is
                }
            }
            buf += out;
        }
        buf += '\n';
    }
    buf += '\n';
    buf += "Lit empty cells appear as '.'  Walls/obstacles block the light.\n";

    move_cursor_home();
    DWORD written = 0;
    WriteConsoleA(g_hOut, buf.data(), (DWORD)buf.size(), &written, NULL);
}

static void try_move(int dx, int dy)
{
    int nx = g_px + dx;
    int ny = g_py + dy;
    if (nx < 0 || nx >= g_W || ny < 0 || ny >= g_H) return;
    if (blocks_move(g_map[ny][nx])) return;
    g_px = nx;
    g_py = ny;
}

int main()
{
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    if (!load_map("map.txt"))
    {
        std::printf("Cannot open map.txt. Make sure map.txt is in the working directory.\n");
        std::printf("Press any key to exit...\n");
        _getch();
        return 1;
    }

    if (!find_start(g_px, g_py))
    {
        std::printf("No empty starting cell found in the map.\n");
        _getch();
        return 1;
    }

    hide_cursor();
    // Clear once so any leftover terminal text doesn't bleed into the frame.
    system("cls");

    compute_light();
    render();

    while (true)
    {
        int k = _getch();
        if (k == 27) break;  // ESC

        int dx = 0, dy = 0;
        if (k == 0 || k == 224)
        {
            int k2 = _getch();
            switch (k2)
            {
                case 72: dy = -1; break;  // Up
                case 80: dy = +1; break;  // Down
                case 75: dx = -1; break;  // Left
                case 77: dx = +1; break;  // Right
                default: break;
            }
        }
        else
        {
            switch (k)
            {
                case 'w': case 'W': dy = -1; break;
                case 's': case 'S': dy = +1; break;
                case 'a': case 'A': dx = -1; break;
                case 'd': case 'D': dx = +1; break;
                case 'q': case 'Q': return 0;
                default: break;
            }
        }

        if (dx != 0 || dy != 0)
        {
            try_move(dx, dy);
            compute_light();
            render();
        }
    }

    move_cursor_home();
    std::printf("\nBye.\n");
    return 0;
}
