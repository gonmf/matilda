#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include "matilda.h"

#include "types.h"

#define N         BOARD_SIZ
#define N_SIMS    10000

#define W         (N+2)
#define BOARDSIZE ((N+1)*W+1)
#define BOARD_IMIN (N+1)
#define BOARD_IMAX (BOARDSIZE-N-1)
#define LARGE_BOARDSIZE ((N+14)*(N+7))
#define BUFLEN 256
#define MAX_GAME_LEN (N*N*3)
#define SINGLEPT_OK       1
#define SINGLEPT_NOK      0
#define TWOLIBS_TEST      1
#define TWOLIBS_TEST_NO   0
#define TWOLIBS_EDGE_ONLY 1
#define RAVE_EQUIV 3500

#define PRIOR_EVEN         10
#define PRIOR_SELFATARI    10
#define PRIOR_CAPTURE_ONE  15
#define PRIOR_CAPTURE_MANY 30
#define PRIOR_PAT3         10

static d32          PRIOR_CFG[] =     {24, 22, 8};
#define LEN_PRIOR_CFG      (sizeof(PRIOR_CFG)/sizeof(d32))
#define PRIOR_EMPTYAREA    10
#define PROB_HEURISTIC_CAPTURE 0.9
#define PROB_HEURISTIC_PAT3    0.95
#define PROB_SSAREJECT 0.9
#define PROB_RSAREJECT 0.5

typedef enum { PASS_MOVE, RESIGN_MOVE, COMPUTER_BLACK, COMPUTER_WHITE } Code;

typedef struct {
    char  color[BOARDSIZE];
    u8  env4[BOARDSIZE];
    u8  env4d[BOARDSIZE];
    d32   n;
    u32 ko, ko_old;
    u32 last, last2;
    float komi;
    char  cap;
    char  capX;
} Position;

typedef struct tree_node {
    d32 v;
    d32 w;
    d32 pv;
    d32 pw;
    d32 av;
    d32 aw;
    d32 nchildren;
    Position pos;
    struct tree_node ** children;
} TreeNode;

typedef struct {
    d32        value;
    d32        in_use;
    d32        mark[BOARDSIZE];
} Mark;

static u8 pat3set[8192];
static d32  npat3;
static Mark *mark1, *mark2;
static u32 idum;

static d32 delta[] = { -N-1, 1, N+1, -1, -N, W, N, -W, 0};
static char buf[BUFLEN];
static u32 allpoints[BOARDSIZE];

static d32 fix_atari(Position * pos, u32 pt, d32 singlept_ok, d32 twolib_test,
    d32 twolib_edgeonly, u32 * moves, u32 * sizes);

#define FORALL_POINTS(pos,i) for(u32 i=BOARD_IMIN ; i<BOARD_IMAX ; i++)
#define FORALL_NEIGHBORS(pos, pt, k, n) \
    for(k=0,n=pt+delta[0] ; k<4 ; n=pt+delta[++k])
#define FORALL_DIAGONAL_NEIGHBORS(pos, pt, k, n) \
    for(k=4,n=pt+delta[4] ; k<8 ; n=pt+delta[++k])
#define FORALL_IN_SLIST(l, item) \
    for(d32 _k=1,_n=l[0],item=l[1] ; _k<=_n ; item=l[++_k])
#define SWAP_CASE(c) {if(c == 'X') c = 'x'; else if (c == 'x') c = 'X'; }
#define SWAP(T, u, v) {T _t = u; u = v; v = _t;}

#define SHUFFLE(T, l, n) for(d32 _k=n-1 ; _k>0 ; _k--) {  \
    d32 _tmp=random_int(_k); SWAP(T, l[_k], l[_tmp]); \
}

static u32 qdrandom(void) {idum=(1664525*idum)+1013904223; return idum;}
static u32 random_int(d32 n) /* random d32 between 0 and n-1 */ \
           {unsigned long long r=qdrandom(); return (r*n)>>32;}

static d32  slist_size(u32 * l) {return l[0];}
static void slist_clear(u32 * l) {l[0]=0;}
static void slist_push(u32 * l, u32 item) {l[l[0]+1]=item;l[0]++;}
static void slist_shuffle(u32 * l) { u32 *t=l+1; SHUFFLE(u32,t,l[0]); }

static d32  slist_insert(u32 * l, u32 item)
{
    d32 k, n=l[0] + 1;
    l[n] = item;
    for (k=1 ; k<=n ; k++)
        if(l[k] == item) break;
    if(k == n) {
        l[0] = n; return 1;
    }
    else return 0;
}

static void mark_init(Mark *m) {
    m->in_use = 1;
    m->value++;
}

static void mark_release(Mark *m) {
    m->in_use = 0;
}

static void mark(Mark *m, u32 i) {
    m->mark[i] = m->value;
}

static d32 is_marked(Mark *m, u32 i) {
    return m->mark[i] == m->value;
}

static u8 bit[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

static d32 pat3_match(Position * pos, u32 pt)
{
    d32 env8=(pos->env4d[pt] << 8) + pos->env4[pt], q=env8 >> 3, r=env8 & 7;
    return (pat3set[q] & bit[r]) != 0;
}

static char pat3src[][10] = {
    "XOX"
    "..."
    "???",
    "XO."
    "..."
    "?.?",
    "XO?"
    "X.."
    "x.?",
    ".O."
    "X.."
    "...",
    "XO?"
    "O.o"
    "?o?",
    "XO?"
    "O.X"
    "???",
    "?X?"
    "O.O"
    "ooo",
    "OX?"
    "o.O"
    "???",
    "X.?"
    "O.?"
    "##?",
    "OX?"
    "X.O"
    "###",
    "?X?"
    "x.O"
    "###",
    "?XO"
    "x.x"
    "###",
    "?OX"
    "X.O"
    "###",
    "###"
    "###"
    "###"
};

static d32 code(char color, d32 p)
{
    d32 code_W[4] = {   0,    0,    0,    0};
    d32 code_B[4] = {0x01, 0x02, 0x04, 0x08};
    d32 code_E[4] = {0x10, 0x20, 0x40, 0x80};
    d32 code_O[4] = {0x11, 0x22, 0x44, 0x88};
    switch(color) {
        case 'O': return code_W[p];
        case 'X': return code_B[p];
        case '.': return code_E[p];
        case '#': return code_O[p];
    }
    return 0;
}

static d32 compute_code(char * src)
{
    d32 env8=0;
    env8 |= code(src[1], 0);
    env8 |= code(src[5], 1);
    env8 |= code(src[7], 2);
    env8 |= code(src[3], 3);

    env8 |= code(src[2], 0)<<8;
    env8 |= code(src[8], 1)<<8;
    env8 |= code(src[6], 2)<<8;
    env8 |= code(src[0], 3)<<8;
    return env8;
}

static void pat_wildexp(char * src, d32 i)
{
    char src1[10];
    d32 env8;
    if ( i==9 ) {
        env8 = compute_code(src);
        d32 q = env8 >> 3, r = env8 & 7;
        pat3set[q] |= bit[r];
        return;
    }
    if (src[i] == '?') {
        strcpy(src1, src);
        src1[i] = 'X'; pat_wildexp(src1, i+1);
        src1[i] = 'O'; pat_wildexp(src1, i+1);
        src1[i] = '.'; pat_wildexp(src1, i+1);
        src1[i] = '#'; pat_wildexp(src1, i+1);
    }
    else if (src[i] == 'x') {
        strcpy(src1, src);
        src1[i]='O'; pat_wildexp(src1, i+1);
        src1[i]='.'; pat_wildexp(src1, i+1);
        src1[i]='#'; pat_wildexp(src1, i+1);
    }
    else if (src[i] == 'o') {
        strcpy(src1, src);
        src1[i]='X'; pat_wildexp(src1, i+1);
        src1[i]='.'; pat_wildexp(src1, i+1);
        src1[i]='#'; pat_wildexp(src1, i+1);
    }
    else
        pat_wildexp(src, i+1);
}

static char *swapcolor(char * src)
{
    for (d32 i=0 ; i<9 ; i++) {
        switch (src[i]) {
            case 'X': src[i] = 'O'; break;
            case 'O': src[i] = 'X'; break;
            case 'x': src[i] = 'o'; break;
            case 'o': src[i] = 'x'; break;
        }
    }
    return src;
}

static char * horizflip(char * src)
{
    SWAP(char, src[0], src[6]);
    SWAP(char, src[1], src[7]);
    SWAP(char, src[2], src[8]);
    return src;
}

static char * vertflip(char * src)
{
    SWAP(char, src[0], src[2]);
    SWAP(char, src[3], src[5]);
    SWAP(char, src[6], src[8]);
    return src;
}

static char * rot90(char * src)
{
    char t=src[0]; src[0]=src[2]; src[2]=src[8]; src[8]=src[6]; src[6]=t;
    t=src[1]; src[1]=src[5]; src[5]=src[7]; src[7]=src[3]; src[3]=t;
    return src;
}

static void pat_enumerate3(char * src)
{
    char src1[10];
    pat_wildexp(src, 0);
    strcpy(src1,src);
    pat_wildexp(swapcolor(src1), 0);
}

static void pat_enumerate2(char * src)
{
    char src1[10];
    pat_enumerate3(src);
    strcpy(src1, src);
    pat_enumerate3(horizflip(src1));
}

static void pat_enumerate1(char * src)
{
    char src1[10];
    pat_enumerate2(src);
    strcpy(src1, src);
    pat_enumerate2(vertflip(src1));
}

static void pat_enumerate(char * src)
{
    char src1[10];
    pat_enumerate1(src);
    strcpy(src1, src);
    pat_enumerate1(rot90(src1));
}

static void make_pat3set(void)
{
    npat3 = sizeof(pat3src) / 10 - 1;
    if (npat3 > 13) {
        fprintf(stderr,"Error npat3 too big (%d)\n", npat3);
        exit(-1);
    }
    memset(pat3set,0,8192);
    for(d32 p=0 ; strcmp(pat3src[p], "#########") != 0 ; p++) {
        pat_enumerate(pat3src[p]);
    }
}

static char is_eyeish(Position * pos, u32 pt)
{
    char eyecolor=0, othercolor=0;
    d32 k;
    u32 n;
    FORALL_NEIGHBORS(pos, pt, k, n) {
        char c = pos->color[n];
        if(c == ' ') continue;
        if(c == '.') return 0;
        if(eyecolor == 0) {
            eyecolor = c;
            othercolor = c; SWAP_CASE(othercolor);
        }
        else if (c == othercolor) return 0;
    }
    return eyecolor;
}

static char is_eye(Position * pos, u32 pt)
{
    char eyecolor=is_eyeish(pos, pt), falsecolor=eyecolor;
    d32 at_edge=0, false_count=0, k;
    u32 d;
    if (eyecolor == 0) return 0;


    SWAP_CASE(falsecolor);
    FORALL_DIAGONAL_NEIGHBORS(pos, pt, k, d) {
        if(pos->color[d] == ' ') at_edge = 1;
        else if(pos->color[d] == falsecolor) false_count += 1;
    }
    if (at_edge) false_count += 1;
    if (false_count >= 2) return 0;
    return eyecolor;
}

static u8 compute_env4(Position * pos, u32 pt, d32 offset)
{
    u8 env4=0, hi, lo, c;
    for (d32 k=offset ; k<offset+4 ; k++) {
        u32 n = pt + delta[k];

        if (pos->color[n] == '.')         c = 2;
        else if(pos->color[n] == ' ')     c = 3;
        else {
            if (pos->n%2==0) {
                if (pos->color[n] == 'X') c = 1;
                else                      c = 0;
            }
            else {
                if (pos->color[n] == 'X') c = 0;
                else                      c = 1;
            }
        }
        hi = c >> 1; lo = c & 1;
        env4 |= ((hi<<4)+lo) << (k-offset);
    }
    return env4;
}

static void put_stone(Position * pos, u32 pt)
{
    if (pos->n%2 == 0) {
        pos->env4[pt+N+1] ^= 0x11;
        pos->env4[pt-1]   ^= 0x22;
        pos->env4[pt-N-1] ^= 0x44;
        pos->env4[pt+1]   ^= 0x88;
        pos->env4d[pt+N]  ^= 0x11;
        pos->env4d[pt-W]  ^= 0x22;
        pos->env4d[pt-N]  ^= 0x44;
        pos->env4d[pt+W]  ^= 0x88;
    }
    else {
        pos->env4[pt+N+1] &= 0xEE;
        pos->env4[pt-1]   &= 0xDD;
        pos->env4[pt-N-1] &= 0xBB;
        pos->env4[pt+1]   &= 0x77;
        pos->env4d[pt+N]  &= 0xEE;
        pos->env4d[pt-W]  &= 0xDD;
        pos->env4d[pt-N]  &= 0xBB;
        pos->env4d[pt+W]  &= 0x77;
    }
    pos->color[pt] = 'X';
}

static void remove_stone(Position * pos, u32 pt)
{
    if (pos->n%2 == 0) {
        pos->env4[pt+N+1] |= 0x10;
        pos->env4[pt-1]   |= 0x20;
        pos->env4[pt-N-1] |= 0x40;
        pos->env4[pt+1]   |= 0x80;
        pos->env4d[pt+N]  |= 0x10;
        pos->env4d[pt-W]  |= 0x20;
        pos->env4d[pt-N]  |= 0x40;
        pos->env4d[pt+W]  |= 0x80;
    }
    else {
        pos->env4[pt+N+1] ^= 0x11;
        pos->env4[pt-1]   ^= 0x22;
        pos->env4[pt-N-1] ^= 0x44;
        pos->env4[pt+1]   ^= 0x88;
        pos->env4d[pt+N]  ^= 0x11;
        pos->env4d[pt-W]  ^= 0x22;
        pos->env4d[pt-N]  ^= 0x44;
        pos->env4d[pt+W]  ^= 0x88;
    }
    pos->color[pt] = '.';
}

static char * empty_position(Position * pos)
{
    d32 k = 0;
    for (d32 col=0 ; col<=N ; col++) pos->color[k++] = ' ';
    for (d32 row=1 ; row<=N ; row++) {
        pos->color[k++] = ' ';
        for (d32 col=1 ; col<=N ; col++) pos->color[k++] = '.';
    }
    for (d32 col=0 ; col<W ; col++) pos->color[k++] = ' ';
    FORALL_POINTS(pos, pt) {
        if (pos->color[pt] == ' ') continue;
        pos->env4[pt] = compute_env4(pos, pt, 0);
        pos->env4d[pt] = compute_env4(pos, pt, 4);
    }

    pos->ko = pos->last = pos->last2 = 0;
    pos->capX = pos->cap = 0;
    pos->n = 0; pos->komi = 7.5;
    return "";
}

static void compute_block(Position * pos, u32 pt, u32 * stones, u32 * libs, d32 nlibs)
{
    char  color=pos->color[pt];
    d32   head=2, k, tail=1;
    u32 n;

    mark_init(mark1);
    slist_clear(libs);
    stones[1] = pt; mark(mark1, pt);
    while(head>tail) {
        pt = stones[tail++];
        FORALL_NEIGHBORS(pos, pt, k, n)
            if (!is_marked(mark1, n)) {
                mark(mark1, n);
                if (pos->color[n] == color)    stones[head++] = n;
                else if (pos->color[n] == '.') {
                    slist_push(libs, n);
                    if (slist_size(libs) >= nlibs) goto finished;
                }
            }
    }
finished:
    stones[0] = head-1;
    mark_release(mark1);
}

static d32 capture_block(Position * pos, u32 * stones)
{
    FORALL_IN_SLIST(stones, pt) remove_stone(pos, pt);
    return slist_size(stones);
}

static void swap_color(Position * pos)
{
    FORALL_POINTS(pos, pt)
        SWAP_CASE(pos->color[pt]);
}

static void remove_X_stone(Position * pos, u32 pt)
{
    (pos->n)++;
    remove_stone(pos, pt);
    (pos->n)--;
}

static char * play_move(Position * pos, u32 pt)
{
    d32   captured=0, k;
    u32 libs[BOARDSIZE], n, stones[BOARDSIZE], pos_capture;

    pos->ko_old = pos->ko;
    if (pt == pos->ko) return "Error Illegal move: retakes ko";
    d32 in_enemy_eye = is_eyeish(pos, pt);

    put_stone(pos, pt);

    pos_capture = 0;
    FORALL_NEIGHBORS(pos, pt, k, n) {
        if (pos->color[n] != 'x') continue;
        compute_block(pos,n,stones, libs, 1);
        if (slist_size(libs)==0) {
            captured += capture_block(pos, stones);
            pos_capture = n;
        }

    }
    if (captured) {
        if (captured==1 && in_enemy_eye) pos->ko = pos_capture;
        else                             pos->ko = 0;
    }
    else {
        pos->ko = 0;
        compute_block(pos, pt, stones, libs, 1);
        if(slist_size(libs) == 0) {
            pos->ko = pos->ko_old;
            remove_X_stone(pos, pt);
            return "Error Illegal move: suicide";
        }
    }

    captured += pos->capX;
    pos->capX = pos->cap;
    pos->cap  = captured;
    swap_color(pos);
    (pos->n)++;
    pos->last2 = pos->last;
    pos->last  = pt;
    return "";
}

static char * pass_move(Position * pos)
{
    swap_color(pos); (pos->n)++;
    pos->last2 = pos->last;
    pos->last  = pos->ko = 0;
    SWAP(d32,pos->cap, pos->capX);
    return "";
}

static void make_list_neighbors(Position * pos, u32 pt, u32 * points)
{
    slist_clear(points);
    if (pt == PASS_MOVE) return;
    slist_push(points, pt);
    for (d32 k=0 ; k<8 ; k++)
        if (pos->color[pt+delta[k]] != ' ')
            slist_push(points, pt+delta[k]);
    slist_shuffle(points);
}

static void make_list_last_moves_neighbors(Position * pos, u32 * points)
{
    u32 last2_neighbors[12];
    make_list_neighbors(pos, pos->last,points);
    make_list_neighbors(pos, pos->last2,last2_neighbors);
    FORALL_IN_SLIST(last2_neighbors, n)
        slist_insert(points, n);
}

static void make_list_neighbor_blocks_in_atari(Position * pos, u32 * stones,
        u32 * breps, u32 * libs)
{
    char  color = pos->color[stones[1]];
    d32   k, maxlibs=2;
    u32 n, st[BOARDSIZE], l[4];

    if (color == 'x') color = 'X';
    else              color = 'x';

    mark_init(mark2); slist_clear(breps); slist_clear(libs);
    FORALL_IN_SLIST(stones, pt) {
        FORALL_NEIGHBORS(pos, pt, k, n) {
            if (pos->color[n] == color && !is_marked(mark2, n)) {
                compute_block(pos, n, st, l, maxlibs);
                if (slist_size(l) == 1) {
                    slist_push(breps, st[1]);
                    slist_push(libs, l[1]);
                    FORALL_IN_SLIST(st, p)
                        mark(mark2, p);
                }
            }
        }
    }
    mark_release(mark2);
}

static double score(Position * pos, d32 owner_map[])
{
    double s=pos->komi;
    d32    n=-1;
    if (pos->n%2==0) {
        s = -s;
        n = 1;
    }

    FORALL_POINTS(pos,pt) {
        char c = pos->color[pt];

        if (c=='.') c = is_eyeish(pos,pt);
        if (c=='X') {
            s += 1.0;
            owner_map[pt] += n;
        }
        else if (c=='x') {
            s -= 1.0;
            owner_map[pt] -= n;
        }
    }
    return s;
}

static u32 read_ladder_attack(Position * pos, u32 pt, u32 * libs)
{
    u32 moves[5], sizes[5];
    u32 move=0;
    FORALL_IN_SLIST(libs, l) {
        Position pos_l = * pos;
        char *ret = play_move(&pos_l, l);
        if (ret[0]!=0) continue;


        slist_clear(moves); slist_clear(sizes);
        d32 is_atari = fix_atari(&pos_l, pt, SINGLEPT_NOK, TWOLIBS_TEST_NO
                                                           , 0, moves, sizes);

        if (is_atari && slist_size(moves) == 0)
            move = l;
    }
    return move;
}

static d32 line_height(u32 pt);
static d32 fix_atari(Position * pos, u32 pt, d32 singlept_ok
        , d32 twolib_test, d32 twolib_edgeonly, u32 * moves, u32 * sizes)
{
    d32 in_atari=1, maxlibs=3;
    u32 stones[BOARDSIZE], l, libs[5], blocks[256], blibs[256];

    slist_clear(moves); slist_clear(sizes);
    compute_block(pos, pt, stones, libs, maxlibs);
    if (singlept_ok && slist_size(stones) == 1) return 0;
    if (slist_size(libs) >= 2) {
        if (twolib_test && slist_size(libs) == 2 && slist_size(stones) > 1) {
            if (twolib_edgeonly
                   && ((line_height(libs[1]))>0 || (line_height(libs[2]))>0)) {

                return 0;
            }
            else {
                u32 ladder_attack = read_ladder_attack(pos, pt, libs);
                if (ladder_attack) {
                    if(slist_insert(moves, ladder_attack))
                        slist_push(sizes, slist_size(stones));
                }
            }
        }
        return 0;
    }

    if (pos->color[pt] == 'x') {

        if (slist_insert(moves, libs[1]))
            slist_push(sizes, slist_size(stones));
        return in_atari;
    }
    make_list_neighbor_blocks_in_atari(pos, stones, blocks, blibs);
    FORALL_IN_SLIST(blibs, l)
        if (slist_insert(moves, l))
            slist_push(sizes, slist_size(stones));

    l = libs[1];

    Position escpos = * pos;
    char *ret = play_move(&escpos, l);
    if (ret[0]!=0)
        return 1;
    compute_block(&escpos, l, stones, libs, maxlibs);
    if (slist_size(libs) >= 2) {
        if (slist_size(moves)>1
        || (slist_size(libs)==2 && read_ladder_attack(&escpos,l,libs) == 0)
        || (slist_size(libs)>=3))
            if (slist_insert(moves, l))
                slist_push(sizes, slist_size(stones));
    }
    return in_atari;
}

static void compute_cfg_distances(Position * pos, u32 pt, char cfg_map[BOARDSIZE])
{
    d32   head=1, k, tail=0;
    u32 fringe[30*BOARDSIZE], n;

    memset(cfg_map, -1, BOARDSIZE);
    cfg_map[pt] = 0;

    fringe[0]=pt;
    while(head > tail) {
        pt = fringe[tail++];
        FORALL_NEIGHBORS(pos, pt, k, n) {
            char c = pos->color[n];
            if (c==' ') continue;
            if (0 <= cfg_map[n] && cfg_map[n] <= cfg_map[pt]) continue;
            d32 cfg_before = cfg_map[n];
            if (c != '.' && c==pos->color[pt])
                cfg_map[n] = cfg_map[pt];
            else
                cfg_map[n] = cfg_map[pt]+1;
            if (cfg_before < 0 || cfg_before > cfg_map[n]) {
                fringe[head++] = n;
            }
        }
    }
}

static d32 line_height(u32 pt)
{
    div_t d = div(pt,N+1);
    d32 row = d.quot, col=d.rem;
    if (row > N/2) row = N+1-row;
    if (col > N/2) col = N+1-col;
    if (row < col) return row-1;
    else           return col-1;
}

static d32 empty_area(Position * pos, u32 pt, d32 dist)
{
    d32   k;
    u32 n;
    FORALL_NEIGHBORS(pos, pt, k, n) {
        if (pos->color[n]=='x' || pos->color[n]=='X')
            return 0;
        else if (pos->color[n]=='.' && dist>1 && !empty_area(pos, n, dist-1))
            return 0;
    }
    return 1;
}

static d32 gen_playout_moves_capture(Position * pos, u32 * heuristic_set, float prob,
                                    d32 expensive_ok, u32 * moves, u32 * sizes)
{
    d32   k, twolib_edgeonly = !expensive_ok;
    u32 move2[20], size2[20];

    slist_clear(moves); slist_clear(sizes);
    if (random_int(10000) <= prob*10000.0)
        FORALL_IN_SLIST(heuristic_set, pt)
            if (pos->color[pt]=='x' || pos->color[pt]=='X') {
                fix_atari(pos, pt, SINGLEPT_NOK, TWOLIBS_TEST,
                                                twolib_edgeonly, move2, size2);
                k=1;
                FORALL_IN_SLIST(move2, move)
                    if (slist_insert(moves, move))
                        slist_push(sizes, size2[k++]);
            }
    return slist_size(moves);
}

static d32 gen_playout_moves_pat3(Position * pos, u32 * heuristic_set, float prob,
                                                                u32 * moves)
{
    slist_clear(moves);
    if (random_int(1000) <= prob*1000.0)
        FORALL_IN_SLIST(heuristic_set, pt)
            if (pos->color[pt] == '.' && pat3_match(pos, pt))
               slist_push(moves, pt);
    return slist_size(moves);
}

static d32 gen_playout_moves_random(Position * pos, u32 moves[BOARDSIZE], u32 i0)
{
    slist_clear(moves);
    for(u32 i=i0 ; i<BOARD_IMAX ; i++) {
        if (pos->color[i] != '.') continue;
        if (is_eye(pos,i) == 'X') continue;
        slist_push(moves, i);
    }
    for(u32 i=BOARD_IMIN-1 ; i<i0 ; i++) {
        if (pos->color[i] != '.') continue;
        if (is_eye(pos,i) == 'X') continue;
        slist_push(moves, i);
    }
    return slist_size(moves);
}

static u32 choose_from(Position * pos, u32 * moves, char *kind)
{
    char   *ret;
    u32   sizes[20];
    u32  move = PASS_MOVE, ds[20];
    Position saved_pos = * pos;

    FORALL_IN_SLIST(moves, pt) {
        ret = play_move(pos, pt);
        if (ret[0] == 0) {
            move = pt;

            d32 r = random_int(10000), tstrej;
            if (strcmp(kind,"random") == 0) tstrej = r<=10000.0*PROB_RSAREJECT;
            else                            tstrej = r<= 10000.0*PROB_SSAREJECT;
            if (tstrej) {
                slist_clear(ds); slist_clear(sizes);
                fix_atari(pos, pt, SINGLEPT_OK, TWOLIBS_TEST, 1, ds, sizes);
                if (slist_size(ds) > 0) {
                    * pos = saved_pos;
                    move = PASS_MOVE;
                    continue;
                }
            }
            break;
        }
    }
    return move;
}

static double mcplayout(Position * pos, d32 amaf_map[], d32 owner_map[])
{
    double s=0.0;
    d32    passes=0, start_n=pos->n;
    u32   sizes[BOARDSIZE];
    u32  last_moves_neighbors[20], moves[BOARDSIZE], move;

    while (passes < 2 && pos->n < MAX_GAME_LEN) {
        move = 0;
        make_list_last_moves_neighbors(pos, last_moves_neighbors);

        if (gen_playout_moves_capture(pos, last_moves_neighbors,
                               PROB_HEURISTIC_CAPTURE, 0, moves, sizes))
            if((move=choose_from(pos, moves, "capture")) != PASS_MOVE)
                goto found;


        if (gen_playout_moves_pat3(pos, last_moves_neighbors,
                                           PROB_HEURISTIC_PAT3, moves))
            if((move=choose_from(pos, moves, "pat3")) != PASS_MOVE)
                goto found;

        gen_playout_moves_random(pos, moves, BOARD_IMIN-1+random_int(N*W));
        move=choose_from(pos, moves, "random");
found:
        if (move == PASS_MOVE) {
            pass_move(pos);
            passes++;
        }
        else {
            if (amaf_map[move] == 0)

                amaf_map[move] = ((pos->n-1)%2==0 ? 1 : -1);
            passes=0;
        }
    }
    s = score(pos, owner_map);
    if (start_n%2 != pos->n%2) s = -s;
    return s;
}

static TreeNode* new_tree_node(Position * pos)
{
    TreeNode *node = calloc(1,sizeof(TreeNode));
    node->pos = * pos;
    node->pv = PRIOR_EVEN; node->pw = PRIOR_EVEN/2;
    return node;
}

static void expand(TreeNode *tree)
{
    char     cfg_map[BOARDSIZE];
    d32      nchildren = 0;
    u32     sizes[BOARDSIZE];
    u32    moves[BOARDSIZE];
    Position pos2;
    TreeNode *childset[BOARDSIZE], *node;
    if (tree->pos.last!=PASS_MOVE)
        compute_cfg_distances(&tree->pos, tree->pos.last, cfg_map);

    gen_playout_moves_random(&tree->pos, moves, BOARD_IMIN-1);

    tree->children = calloc(slist_size(moves)+2, sizeof(TreeNode*));
    FORALL_IN_SLIST(moves, pt) {
        pos2 = tree->pos;
        char * ret = play_move(&pos2, pt);
        if (ret[0] != 0) continue;

        childset[pt]= tree->children[nchildren++] = new_tree_node(&pos2);
    }
    tree->nchildren = nchildren;

    gen_playout_moves_capture(&tree->pos, allpoints, 1, 1, moves, sizes);
    d32 k=1;
    FORALL_IN_SLIST(moves, pt) {
        pos2 = tree->pos;
        char * ret = play_move(&pos2, pt);
        if (ret[0] != 0) continue;
        node = childset[pt];
        if (sizes[k] == 1) {
            node->pv += PRIOR_CAPTURE_ONE;
            node->pw += PRIOR_CAPTURE_ONE;
        }
        else {
            node->pv += PRIOR_CAPTURE_MANY;
            node->pw += PRIOR_CAPTURE_MANY;
        }
        k++;
    }
    gen_playout_moves_pat3(&tree->pos, allpoints, 1, moves);
    FORALL_IN_SLIST(moves, pt) {
        pos2 = tree->pos;
        char * ret = play_move(&pos2, pt);
        if (ret[0] != 0) continue;
        node = childset[pt];
        node->pv += PRIOR_PAT3;
        node->pw += PRIOR_PAT3;
    }

    for (d32 k=0 ; k<tree->nchildren ; k++) {
        node = tree->children[k];
        u32 pt = node->pos.last;

        if (tree->pos.last != PASS_MOVE && cfg_map[pt] - 1 < (d32)LEN_PRIOR_CFG) {
            node->pv += PRIOR_CFG[cfg_map[pt]-1];
            node->pw += PRIOR_CFG[cfg_map[pt]-1];
        }

        d32 height = line_height(pt);
        if (height <= 2 && empty_area(&tree->pos, pt, 3)) {
            if (height <= 1) {
                node->pv += PRIOR_EMPTYAREA;
                node->pw += 0;
            }
            if (height == 2) {
                node->pv += PRIOR_EMPTYAREA;
                node->pw += PRIOR_EMPTYAREA;
            }
        }

        fix_atari(&node->pos, pt, SINGLEPT_OK, TWOLIBS_TEST, !TWOLIBS_EDGE_ONLY,
                                                                 moves, sizes);
        if (slist_size(moves) > 0) {
            node->pv += PRIOR_SELFATARI;
            node->pw += 0;
        }
    }

    if (tree->nchildren == 0) {

        pos2 = tree->pos;
        pass_move(&pos2);
        tree->children[0] = new_tree_node(&pos2);
        tree->nchildren = 1;
    }
}

static void free_tree(TreeNode *tree)
{
    if (tree->children != NULL) {
        for (TreeNode **child = tree->children ; *child != NULL ; child++)
                free_tree(*child);
        free(tree->children);
    }
    free(tree);
}

static double rave_urgency(TreeNode *node)
{
    double v = node->v + node->pv;
    double expectation = (node->w + node->pw)/v;
    if (node->av==0) return expectation;

    double rave_expectation = (double) node->aw / (double) node->av;
    double beta = node->av / (node->av + v + (double)v*node->av/RAVE_EQUIV);
    return beta * rave_expectation + (1-beta) * expectation;
}

static TreeNode* best_move(TreeNode *tree, TreeNode **except)
{
    d32 vmax=-1;
    TreeNode *best=NULL;

    if (tree->children == NULL) return NULL;

    for (TreeNode **child = tree->children ; *child != NULL ; child++) {
        if ((*child)->v > vmax) {
            d32 update = 1;
            if (except != NULL)
                for (TreeNode **n=except ; *n!=NULL ; n++)
                    if (*child == *n) update=0;
            if (update) {
                vmax = (*child)->v;
                best = (*child);
            }
        }
    }
    return best;
}

static TreeNode* most_urgent(TreeNode **children, d32 nchildren)
{
    d32    k=0;
    double urgency, umax=0;
    TreeNode *urgent = children[0];


    SHUFFLE(TreeNode *, children, nchildren);

    for (TreeNode **child = children ; *child != NULL ; child++) {
        urgency = rave_urgency(*child);
        if (urgency > umax) {
            umax = urgency;
            urgent = *child;
        }
        k++;
    }
    return urgent;
}

static d32 tree_descend(TreeNode *tree, d32 amaf_map[], TreeNode **nodes)
{
    d32 last=0, passes = 0;
    u32 move;

    nodes[last] = tree;

    while (nodes[last]->children != NULL && passes <2) {

        TreeNode *node = most_urgent(nodes[last]->children, nodes[last]->nchildren);
        nodes[++last] = node;
        move = node->pos.last;

        if (move == PASS_MOVE) passes++;
        else {
            passes = 0;
            if (amaf_map[move] == 0)
                amaf_map[move] = (nodes[last-1]->pos.n%2==0 ? 1 : -1);
        }

        if (node->children == NULL && node->v >= 1)
            expand(node);
    }
    return last;
}

static void tree_update(TreeNode **nodes,d32 last,d32 amaf_map[],double score)
{
    for (d32 k=last ; k>=0 ; k--) {
        TreeNode *n= nodes[k];
        n->v += 1;
        n->w += score < 0.0;



        d32 amaf_map_value = (n->pos.n %2 == 0 ? 1 : -1);
        if (n->children != NULL) {
            for (TreeNode **child = n->children ; *child != NULL ; child++) {
                if ((*child)->pos.last == 0) continue;
                if (amaf_map[(*child)->pos.last] == amaf_map_value) {
                    (*child)->aw += score > 0;
                    (*child)->av += 1;
                }
            }
        }
        score = -score;
    }
}

static u32 tree_search(TreeNode *tree, d32 n, d32 owner_map[])
{
    double s;
    d32 *amaf_map=calloc(BOARDSIZE, sizeof(d32)), i, last;
    TreeNode *best, *nodes[500];


    if (tree->children == NULL) expand(tree);
    memset(owner_map,0,BOARDSIZE*sizeof(d32));

    for (i=0 ; i<n ; i++) {
        memset(amaf_map, 0, BOARDSIZE*sizeof(d32));
        last = tree_descend(tree, amaf_map, nodes);
        Position pos = nodes[last]->pos;
        s = mcplayout(&pos, amaf_map, owner_map);
        tree_update(nodes, last, amaf_map, s);
    }
    best = best_move(tree, NULL);

    free(amaf_map);
    if (best->pos.last == PASS_MOVE && best->pos.last2 == PASS_MOVE)
        return PASS_MOVE;
    else
        return best->pos.last;
}

static u32 parse_coord(char *s)
{
    char c, str[10];
    d32  x,y;
    for (d32 i=0 ; i<9 ; i++) {
        if (s[i]==0) {
            str[i]=0;
            break;
        }
        str[i] = toupper(s[i]);
    }
    if(strcmp(str, "PASS") == 0) return PASS_MOVE;
    sscanf(s, "%c%d", &c, &y);
    c = toupper(c);
    if (c<'J') x = c-'@';
    else       x = c-'@'-1;
    return (N-y+1)*(N+1) + x;
}

static char * str_coord(u32 pt, char str[8])
{
    if (pt == PASS_MOVE) strcpy(str, "pass");
    else if (pt == RESIGN_MOVE) strcpy(str, "resign");
    else {
        d32 row = pt/(N+1); d32 col = (pt%(N+1));
        sprintf(str, "%c%d", '@'+col,N+1-row);
        if (str[0] > 'H') str[0]++;
    }
    return str;
}

static void gtp_io(void)
{
    char line[BUFLEN], *cmdid, *command, msg[BUFLEN], *ret;
    char *known_commands="\nboardsize\ngenmove\nhelp\nknown_command"
    "\nkomi\nlist_commands\nname\nplay\nprotocol_version\nquit\nversion\n";
    d32      i;
    d32      *owner_map=calloc(BOARDSIZE, sizeof(d32));
    TreeNode *tree;
    Position * pos, pos2;

    pos = &pos2;
    empty_position(pos);
    tree = new_tree_node(pos);

    for(;;) {
        ret = "";
        if (fgets(line, BUFLEN, stdin) == NULL) break;
        line[strlen(line)-1] = 0;
        command = strtok(line, " \t\n");
        if (command == NULL) continue;
        if (command[0] == '#') continue;
        if (sscanf(command, "%d", &i) == 1) {
            cmdid = command;
            command = strtok(NULL, " \t\n");
        }
        else
            cmdid = "";

        if (strcmp(command, "play")==0) {
            ret = strtok(NULL, " \t\n");
            char *str = strtok(NULL, " \t\n");
            if(str == NULL) goto finish_command;
            u32 pt = parse_coord(str);
            if (pos->color[pt] == '.')
                ret = play_move(pos, pt);
            else {
                if(pt == PASS_MOVE) ret = pass_move(pos);
                else ret ="Error Illegal move: point not EMPTY\n";
            }
        }
        else if (strcmp(command, "genmove") == 0) {
            u32 pt;
            if (pos->last == PASS_MOVE && pos->n>2) {
                pt = PASS_MOVE;
            }
            else {
                free_tree(tree);
                tree = new_tree_node(pos);
                pt = tree_search(tree, N_SIMS, owner_map);
            }
            if (pt == PASS_MOVE)
                pass_move(pos);
            else if (pt != RESIGN_MOVE)
                play_move(pos, pt);
            ret = str_coord(pt, buf);
        }
        else if (strcmp(command, "clear_board") == 0) {
            free_tree(tree);
            ret = empty_position(pos);
            tree = new_tree_node(pos);
        }
        else if (strcmp(command, "boardsize") == 0) {
            char *str = strtok(NULL, " \t\n");
            if(str == NULL) goto finish_command;
            d32 size = atoi(str);
            if (size != N) {
                exit(1);
            }
            else
                ret = "";
        }
        else if (strcmp(command, "komi") == 0) {
            char *str = strtok(NULL, " \t\n");
            if(str == NULL) goto finish_command;
            float komi = (float) atof(str);
            if (komi != 7.5) {
                exit(1);
            }
            else
                ret = "";
        }
        else if (strcmp(command,"name") == 0)
            ret = "michi-c";
        else if (strcmp(command,"version") == 0)
            ret = "simple go program demo";
        else if (strcmp(command,"protocol_version") == 0)
            ret = "2";
        else if (strcmp(command,"list_commands") == 0)
            ret = known_commands;
        else if (strcmp(command,"help") == 0)
            ret = known_commands;
        else if (strcmp(command,"known_command") == 0) {
            char *command = strtok(NULL, " \t\n");
            if (strstr(known_commands,command) != NULL)
                ret = "true";
            else
                ret = "false";
        }
        else if (strcmp(command,"quit") == 0) {
            printf("=%s \n\n", cmdid);
            break;
        }
        else {
            sprintf(msg, "Warning: Ignoring unknown command - %s\n", command);
            ret = msg;
        }
finish_command:
        if ((ret[0]=='E' && ret[1]=='r')
                        || ret[0]=='W') printf("\n?%s %s\n\n", cmdid, ret);
        else                            printf("\n=%s %s\n\n", cmdid, ret);
        fflush(stdout);
    }
}

int main()
{
    make_pat3set();
    mark1 = calloc(1, sizeof(Mark));
    mark2 = calloc(1, sizeof(Mark));
    slist_clear(allpoints);

    gtp_io();
    return 0;
}
