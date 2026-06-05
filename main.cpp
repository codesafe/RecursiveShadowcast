#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

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

bool find_start(int& sx, int& sy)
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

inline bool blocks_light(char c)
{
    return c == '#' || c == '*';
}

inline bool blocks_move(char c)
{
    return c == '#' || c == '*';
}

void hide_cursor()
{
    CONSOLE_CURSOR_INFO ci;
    ci.dwSize = 1;
    ci.bVisible = FALSE;
    SetConsoleCursorInfo(g_hOut, &ci);
}

void move_cursor_home()
{
    COORD c = { 0, 0 };
    SetConsoleCursorPosition(g_hOut, c);
}

void try_move(int dx, int dy)
{
    int nx = g_px + dx;
    int ny = g_py + dy;
    if (nx < 0 || nx >= g_W || ny < 0 || ny >= g_H) 
        return;

    if (blocks_move(g_map[ny][nx])) 
        return;

    g_px = nx;
    g_py = ny;
}


void render()
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


#if 1

/*
    Axis
           | -
           |
    -      |       +  
    -------+-------
           |
           |
           | +
*/ 



#define UP      0
#define RIGHT   1
#define DOWN    2
#define LEFT    3
#define DIR_COUNT   4


void scan_shadow(int depth, int direction, double start_slope, double end_slope)
{
    if( depth > LIGHT_RADIUS )
		return;

    int min_col = (int)ceil(depth * start_slope);
    int max_col = (int)floor(depth * end_slope);
    for (int col = min_col; col <= max_col; ++col)
    {
        int tx, ty;
        switch (direction)
        {
            case UP:
                tx = g_px + col;
                ty = g_py - depth;
                break;
            case RIGHT:
                tx = g_px + depth;
                ty = g_py + col;
                break;
            case DOWN:
                tx = g_px + col;
                ty = g_py + depth;
                break;
            case LEFT:
                tx = g_px - depth;
                ty = g_py + col;
                break;
            default:
                return;
        }

        if (tx < 0 || tx >= g_W || ty < 0 || ty >= g_H)
        {
            continue;  // Out of bounds
        }

        bool blocked = blocks_light(g_map[ty][tx]);
        bool in_radius = (col * col + depth * depth) <= (LIGHT_RADIUS * LIGHT_RADIUS);
        if (in_radius && (blocked || (col >= depth * start_slope && col <= depth * end_slope)))
        {
            g_visible[ty][tx] = true;
        }
        if (blocked)
        {
            if (start_slope == -1)
            {
                start_slope = ((2.0 * col - 1.0) / (2.0 * depth));
            }
            else
            {
                end_slope = ((2.0 * col + 1.0) / (2.0 * depth));
                scan_shadow(depth + 1, direction, start_slope, end_slope);
                start_slope = -1;  // Reset
            }
        }
    }
    if (start_slope != -1)
    {
        scan_shadow(depth + 1, direction, start_slope, end_slope);
	}

}


void compute_light()
{
    for (int y = 0; y < g_H; ++y)
    {
        for (int x = 0; x < g_W; ++x)
        {
            g_visible[y][x] = false;
        }
    }
    g_visible[g_py][g_px] = true;

    int direction = UP;

    for (int d = UP; d < DIR_COUNT; d++)
        scan_shadow(1, d, -1.0, 1.0);
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


#else

struct ShadowQuadrant
{
    int xx, xy;
    int yx, yy;
};

struct ShadowRow
{
    int depth;
    double start_slope;
    double end_slope;
};

static inline bool in_bounds(int x, int y)
{
    return x >= 0 && x < g_W && y >= 0 && y < g_H;
}

static inline int round_ties_up(double value)
{
    // Symmetric Shadowcasting 논문/예제에서 쓰는 "0.5는 위로" 반올림이다.
    // 행의 시작 열을 정할 때 좌우 대칭이 깨지지 않게 해 준다.
    return (int)std::floor(value + 0.5);
}

static inline int round_ties_down(double value)
{
    // 끝 열은 "0.5는 아래로" 반올림한다.
    // 시작 열과 짝을 이루어 같은 각도의 타일을 중복/누락 없이 스캔한다.
    return (int)std::ceil(value - 0.5);
}

static inline double tile_slope(int depth, int col)
{
    // 현재 타일의 왼쪽 가장자리 기울기다.
    // 벽을 만났을 때 다음 행의 end_slope로 넘겨 그림자 영역을 잘라낸다.
    return (2.0 * col - 1.0) / (2.0 * depth);
}

static void transform_quadrant(const ShadowQuadrant& q, int depth, int col, int& x, int& y)
{
    // depth는 광원에서 바깥으로 나가는 행 번호, col은 그 행 안의 좌우 위치다.
    // 네 개의 기저 벡터를 바꿔 같은 스캔 코드를 북/동/남/서 사분면에 재사용한다.
    x = g_px + col * q.xx + depth * q.xy;
    y = g_py + col * q.yx + depth * q.yy;
}

static bool is_symmetric(const ShadowRow& row, int col)
{
    // 타일 중심이 현재 스캔 기울기 범위 안에 있으면 대칭적으로 보이는 타일이다.
    // 벽 타일은 중심이 범위 밖이어도 드러내지만, 바닥 타일은 이 조건을 통과해야 한다.
    return col >= row.depth * row.start_slope && col <= row.depth * row.end_slope;
}

static void scan_shadow_row(const ShadowQuadrant& q, const ShadowRow& row)
{
    if (row.depth > LIGHT_RADIUS || row.start_slope > row.end_slope)
    {
        return;
    }

    const int min_col = round_ties_up(row.depth * row.start_slope);
    const int max_col = round_ties_down(row.depth * row.end_slope);
    const int R2 = LIGHT_RADIUS * LIGHT_RADIUS;

    bool has_prev = false;
    bool prev_blocked = false;
    ShadowRow next_row = { row.depth + 1, row.start_slope, row.end_slope };

    for (int col = min_col; col <= max_col; ++col)
    {
        int tx, ty;
        transform_quadrant(q, row.depth, col, tx, ty);

        const bool valid = in_bounds(tx, ty);
        const bool blocked = !valid || blocks_light(g_map[ty][tx]);
        const bool in_radius = col * col + row.depth * row.depth <= R2;

        // Symmetric Shadowcasting은 벽과 바닥을 다르게 처리한다.
        // 벽은 그림자의 시작점이라서 보이면 표시하고, 바닥은 중심이 빛 부채꼴 안에 있을 때만 표시한다.
        if (valid && in_radius && (blocked || is_symmetric(row, col)))
        {
            g_visible[ty][tx] = true;
        }

        if (has_prev)
        {
            if (prev_blocked && !blocked)
            {
                // 벽 뒤에서 다시 바닥이 시작되면, 그 바닥의 왼쪽 가장자리부터 새 빛 부채꼴을 연다.
                next_row.start_slope = tile_slope(row.depth, col);
            }
            else if (!prev_blocked && blocked)
            {
                // 바닥에서 벽으로 바뀌는 순간 그림자가 생긴다.
                // 현재 벽의 왼쪽 가장자리까지만 다음 행을 재귀 스캔한다.
                ShadowRow visible_part = next_row;
                visible_part.end_slope = tile_slope(row.depth, col);
                scan_shadow_row(q, visible_part);
            }
        }

        has_prev = true;
        prev_blocked = blocked;
    }

    if (has_prev && !prev_blocked)
    {
        // 행의 끝까지 열린 공간이면 같은 기울기 범위를 유지한 채 다음 행으로 진행한다.
        scan_shadow_row(q, next_row);
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

    // Symmetric Shadowcasting:
    // 1. 광원 주변을 북/동/남/서 네 사분면으로 나눈다.
    // 2. 각 사분면에서 가까운 행부터 멀리 있는 행까지 스캔한다.
    // 3. 벽을 만나면 다음 행에 넘길 기울기 범위를 잘라 그림자 영역을 만든다.
    const ShadowQuadrant quadrants[] =
    {
        { 1,  0,  0, -1 }, // 북쪽: depth가 커질수록 y가 감소한다.
        { 0,  1,  1,  0 }, // 동쪽: depth가 커질수록 x가 증가한다.
        { 1,  0,  0,  1 }, // 남쪽: depth가 커질수록 y가 증가한다.
        { 0, -1,  1,  0 }, // 서쪽: depth가 커질수록 x가 감소한다.
    };

    for (const ShadowQuadrant& q : quadrants)
    {
        scan_shadow_row(q, { 1, -1.0, 1.0 });
    }
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

#endif