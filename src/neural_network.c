/*
Functions around performing a feed forward neural network to evaluate what move
to play; over a trained two-layer perceptron, with three input units per board
position. The input layer codifies liberties after playing codification and the
connections are made within a limited range to the previous layer.
*/


#include "matilda.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <strings.h>
#include <string.h>
#include <math.h>
#include <omp.h>

#include "alloc.h"
#include "board.h"
#include "cfg_board.h"
#include "engine.h"
#include "flog.h"
#include "move.h"
#include "neural_network.h"
#include "state_changes.h"
#include "tactical.h"
#include "timem.h"

static mlp * default_nn;

static move_seq neighbours[TOTAL_BOARD_SIZ];

/*
Sigmoid logistic function for signal attenuation.
*/
double sigmoid(
    double v
){
    // logistic: return 1.0 / (1.0 + exp(-1.0 * v));
    return HYPERBOLIC_CONSTANT_A * tanh(HYPERBOLIC_CONSTANT_B * v);
}

/*
Feed-forward the energy through the network.
Single-threaded.
*/
void nn_forward_pass_single_threaded(
    mlp * n,
    const double input_units[3][TOTAL_BOARD_SIZ]
){
    /*
    hidden layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        n->output_layer[i].output = 0.0;
        n->hidden_layer[i].output = 0.0;
    }

    /* for each position in layer matrix */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n0 = 0; n0 < neighbours[i].count; ++n0)
        {
            move j = neighbours[i].coord[n0];
            for(u8 p = 0; p < 3; ++p)
            {
                /* for each input matrix */
                n->hidden_layer[i].output += input_units[p][j] *
                    n->hidden_layer[i].weights[p][j];
            }
        }
    /*
    end of hidden layer and start of output layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        n->hidden_layer[i].output = sigmoid(n->hidden_layer[i].output);

    /* for each position in layer matrix */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n0 = 0; n0 < neighbours[i].count; ++n0){
            move j = neighbours[i].coord[n0];
            n->output_layer[i].output += n->hidden_layer[j].output *
                n->output_layer[i].weights[j];
        }
    /* for each position in layer matrix */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        n->output_layer[i].output = sigmoid(n->output_layer[i].output);
}

/*
Feed-forward the energy through the network.
Multi-threaded.
*/
void nn_forward_pass_multi_threaded(
    mlp * n,
    const double input_units[3][TOTAL_BOARD_SIZ]
){
    /*
    hidden layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        n->output_layer[i].output = 0.0;
        n->hidden_layer[i].output = 0.0;
    }

    /* for each position in layer matrix */
    #pragma omp parallel for schedule(dynamic)
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n0 = 0; n0 < neighbours[i].count; ++n0)
        {
            move j = neighbours[i].coord[n0];
            /* for each input matrix */
            for(u8 p = 0; p < 3; ++p)
                n->hidden_layer[i].output += input_units[p][j] *
                    n->hidden_layer[i].weights[p][j];
        }
    /*
    end of hidden layer and start of output layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        n->hidden_layer[i].output = sigmoid(n->hidden_layer[i].output);

    /* for each position in layer matrix */
    #pragma omp parallel for schedule(dynamic)
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n0 = 0; n0 < neighbours[i].count; ++n0)
        {
            move j = neighbours[i].coord[n0];
            n->output_layer[i].output += n->hidden_layer[j].output *
                n->output_layer[i].weights[j];
        }
    /* for each position in layer matrix */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        n->output_layer[i].output = sigmoid(n->output_layer[i].output);
}

/*
Initialize neural network support. Will fail if the neural network file (.nn) is
not found.
*/
void nn_init()
{
    if(default_nn != NULL)
        return;

    init_moves_by_distance(neighbours, NN_CONN_DST, true);

    default_nn = (mlp *)malloc(sizeof(mlp));
    if(default_nn == NULL)
        flog_crit("nn", "system out of memory\n");

    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%dx%d.nn%u", data_folder(), BOARD_SIZ,
        BOARD_SIZ, NN_CONN_DST);
    FILE * fp = fopen(filename, "rb");
    if(fp == NULL)
    {
        char * s = alloc();
        snprintf(s, MAX_PAGE_SIZ, "couldn't open %s for reading\n", filename);
        flog_warn("nn", s);
        release(s);
        release(filename);
        return;
    }
    char * notice = alloc();
    snprintf(notice, MAX_PAGE_SIZ, "read %s", filename);
    release(filename);

    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u8 l = 0; l < 3; ++l)
            for(u16 n = 0; n < neighbours[i].count; ++n)
            {
                move j = neighbours[i].coord[n];
                size_t r = fread(&default_nn->hidden_layer[i].weights[l][j],
                    sizeof(double), 1, fp);
                if(r != 1){
                    fprintf(stderr, "error: nn: file reading error\n");
                    exit(EXIT_FAILURE);
                }
            }
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n = 0; n < neighbours[i].count; ++n){
            move j = neighbours[i].coord[n];
            size_t r = fread(&default_nn->output_layer[i].weights[j],
                sizeof(double), 1, fp);
            if(r != 1)
                flog_crit("nn", "file reading error\n");
        }
    fclose(fp);
    flog_info("nn", notice);
    release(notice);

}

/*
Allocates and initializes a neural network for the current board size.
RETURNS neural network instance
*/
mlp * alloc_instance()
{
    if(default_nn == NULL)
        return NULL;

    mlp * ret = malloc(sizeof(mlp));
    memcpy(ret, default_nn, sizeof(mlp));
    return ret;
}

/*
Initialize the input units.
*/
void nn_populate_input_units(
    double input_units[3][TOTAL_BOARD_SIZ],
    const u8 p[TOTAL_BOARD_SIZ]
){
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i){
        input_units[0][i] = (p[i] & 1) ? 1.0 : 0.0;
        input_units[1][i] = (p[i] & 2) ? 1.0 : 0.0;
        input_units[2][i] = (p[i] & 4) ? 1.0 : 0.0;
    }
}

/*
Codify a board into an array proper for initializing the input unit layer of the
neural network.
*/
void nn_codify_board(
    u8 dst[TOTAL_BOARD_SIZ],
    const board * src,
    bool is_black
){
    u8 liberties;
    u16 stones_captured;
    for(move m = 0; m < TOTAL_BOARD_SIZ; ++m){
        switch(src->p[m]){
            case BLACK_STONE:
                dst[m] = is_black ? NN_BLACK_STONE : NN_WHITE_STONE;
                break;
            case WHITE_STONE:
                dst[m] = is_black ? NN_WHITE_STONE : NN_BLACK_STONE;
                break;
            case EMPTY:
                liberties = libs_after_play_slow(src, is_black, m,
                    &stones_captured);
                switch(liberties){
                    case 0:
                        dst[m] = NN_ILLEGAL;
                        break;
                    case 1:
                        dst[m] = NN_EMPTY_1LIBS;
                        break;
                    case 2:
                        dst[m] = NN_EMPTY_2LIBS;
                        break;
                    case 3:
                        dst[m] = NN_EMPTY_3LIBS;
                        break;
                    case 4:
                        dst[m] = NN_EMPTY_4LIBS;
                        break;
                    default:
                        dst[m] = NN_EMPTY_5PLUSLIBS;
                        break;
                }
                break;
            default:
                flog_crit("nn", "illegal state intersection format in board");
        }
    }
}

/*
Codify a board into an array proper for initializing the input unit layer of the
neural network; benefitting from already having a list of liberties after
playing.
*/
void nn_codify_cfg_board(
    u8 dst[TOTAL_BOARD_SIZ],
    const cfg_board * src,
    bool is_black,
    const u8 liberties_after_playing[TOTAL_BOARD_SIZ]
){
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i){
        switch(src->p[i]){
            case BLACK_STONE:
                dst[i] = is_black ? NN_BLACK_STONE : NN_WHITE_STONE;
                break;
            case WHITE_STONE:
                dst[i] = is_black ? NN_WHITE_STONE : NN_BLACK_STONE;
                break;
            case EMPTY:
                switch(liberties_after_playing[i]){
                    case 0:
                        dst[i] = NN_ILLEGAL;
                        break;
                    case 1:
                        dst[i] = NN_EMPTY_1LIBS;
                        break;
                    case 2:
                        dst[i] = NN_EMPTY_2LIBS;
                        break;
                    case 3:
                        dst[i] = NN_EMPTY_3LIBS;
                        break;
                    case 4:
                        dst[i] = NN_EMPTY_4LIBS;
                        break;
                    default:
                        dst[i] = NN_EMPTY_5PLUSLIBS;
                        break;
                }
                break;
            default:
                flog_crit("nn", "illegal state intersection format in board");
        }
    }
}

/*
Assuming the player color is black, initializes a neural network, performs a
feed-forward pass and updates the output structure.
*/
void neural_network_eval(
    out_board * out_b,
    board * state,
    bool is_black
){
    nn_init();
    mlp * nn = alloc_instance();
    if(nn == NULL){
        flog_crit("nn", "neural network file not available");
    }

    u8 codified_board[TOTAL_BOARD_SIZ];
    nn_codify_board(codified_board, state, is_black);

    double input_units[3][TOTAL_BOARD_SIZ];
    nn_populate_input_units(input_units, (const u8 *)codified_board);
    nn_forward_pass_multi_threaded(nn,
        (const double (*)[TOTAL_BOARD_SIZ])input_units);

    /*
    output board
    */
    out_b->pass = -1.0;
    for(move m = 0; m < TOTAL_BOARD_SIZ; ++m)
        if(can_play_slow(state, m, is_black)){
            out_b->value[m] = 1.0 - fabs(1.0 - nn->output_layer[m].output);
            out_b->tested[m] = true;
        }else
            out_b->tested[m] = false;
}
