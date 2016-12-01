/*
Functions around performing a feed forward neural network to evaluate what move
to play; over a trained two-layer perceptron, with three input units per board
position. The input layer codifies liberties after playing codification and the
connections are made within a limited range to the previous layer.
*/

#ifndef MATILDA_NEURAL_NETWORK_H
#define MATILDA_NEURAL_NETWORK_H

#include "matilda.h"

#include "board.h"
#include "cfg_board.h"
#include "types.h"

/* Trained MLP network */
typedef struct __neuron2_ {
    double weights[3][TOTAL_BOARD_SIZ];
    double output;
} neuron2;

typedef struct __neuron1_ {
    double weights[TOTAL_BOARD_SIZ];
    double output;
} neuron1;

typedef struct __mlp_ {
    neuron2 hidden_layer[TOTAL_BOARD_SIZ];
    neuron1 output_layer[TOTAL_BOARD_SIZ];
} mlp;


/* Perceptive field distance */
#define NN_CONN_DST 8

#define TARGET_VALUE_OFFSET 0.7159
#define HYPERBOLIC_CONSTANT_A 1.7159
#define HYPERBOLIC_CONSTANT_B (2.0 / 3.0)

/* Feature codification (3 bit, 3 matrixes) */
#define NN_BLACK_STONE 2
#define NN_WHITE_STONE 1
#define NN_ILLEGAL 0 /* liberties after playing = 0 */
#define NN_EMPTY_1LIBS 3
#define NN_EMPTY_2LIBS 4
#define NN_EMPTY_3LIBS 5
#define NN_EMPTY_4LIBS 6
#define NN_EMPTY_5PLUSLIBS 7



/*
Initialize neural network support. Will fail if the neural network file (.nn) is
not found.
*/
void nn_init();

/*
Allocates and initializes a neural network for the current board size.
RETURNS neural network instance
*/
mlp * alloc_instance();

/*
Sigmoid logistic function for signal attenuation.
*/
double sigmoid(
    double v
);

/*
Feed-forward the energy through the network.
Single-threaded.
*/
void nn_forward_pass_single_threaded(
    mlp * n,
    const double input_units[3][TOTAL_BOARD_SIZ]
);

/*
Feed-forward the energy through the network.
Multi-threaded.
*/
void nn_forward_pass_multi_threaded(
    mlp * n,
    const double input_units[3][TOTAL_BOARD_SIZ]
);

/*
Initialize the input units.
*/
void nn_populate_input_units(
    double input_units[3][TOTAL_BOARD_SIZ],
    const u8 p[TOTAL_BOARD_SIZ]
);

/*
Codify a board into an array proper for initializing the input unit layer of the
neural network.
*/
void nn_codify_board(
    u8 dst[TOTAL_BOARD_SIZ],
    const board * src,
    bool is_black
);

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
);

/*
Assuming the player color is black, initializes a neural network, performs a
feed-forward pass and updates the output structure.
*/
void neural_network_eval(
    out_board * out_b,
    board * state,
    bool is_black
);

#endif
