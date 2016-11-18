/*
Implementation of neural network training of simple 2-layer MLP with a local
area input for each next layer neuron, and output to the entire board.
*/

#include "matilda.h"

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <omp.h>

#include "alloc.h"
#include "board.h"
#include "data_set.h"
#include "engine.h"
#include "move.h"
#include "neural_network.h"
#include "randg.h"
#include "state_changes.h"
#include "tactical.h"
#include "timem.h"
#include "types.h"

#define LEARNING_RATE 0.002

#define INIT_MIN_WEIGHT 0.0001

#define MAX_TRAINING_SET_SIZE 1000000

typedef struct __lneuron2_ {
    double weights[3][TOTAL_BOARD_SIZ];
    double output;
    double local_gradient;
} lneuron2;

typedef struct __lneuron1_ {
    double weights[TOTAL_BOARD_SIZ];
    double next_weights[TOTAL_BOARD_SIZ];
    double output;
    double local_gradient;
} lneuron1;

static double input_units[3][TOTAL_BOARD_SIZ];
static lneuron2 hidden_layer[TOTAL_BOARD_SIZ];
static lneuron1 output_layer[TOTAL_BOARD_SIZ];
static double desired_output[TOTAL_BOARD_SIZ];

/* accuracy at selecting best play */
static u32 hits;
/*
number of times b.p. found in top plays (depending on number of legal plays)
*/
static u32 selected;
static u32 nr_of_connections;

static move_seq neighbours[TOTAL_BOARD_SIZ];

static void backup_network(
    u32 pass_nr
){
    char * filename = alloc();
    snprintf(filename, MAX_PAGE_SIZ, "%s%ux%u_%u.nn%u", data_folder(), BOARD_SIZ,
        BOARD_SIZ, pass_nr, NN_CONN_DST);

    FILE * fp = fopen(filename, "wb");
    release(filename);
    if(fp == NULL)
    {
        fprintf(stderr, "error: create/open of backup file failed\n");
        return;
    }

    u32 written = 0;
    for(move j = 0; j < TOTAL_BOARD_SIZ; ++j)
        for(u8 l = 0; l < 3; ++l)
            for(u16 n = 0; n < neighbours[j].count; ++n)
            {
                move o = neighbours[j].coord[n];
                size_t w = fwrite(&hidden_layer[j].weights[l][o],
                    sizeof(double), 1, fp);
                if(w != 1)
                {
                    fprintf(stderr, "error: write failed\n");
                    exit(EXIT_FAILURE);
                }
                ++written;
            }
    for(move j = 0; j < TOTAL_BOARD_SIZ; ++j)
        for(u16 n = 0; n < neighbours[j].count; ++n)
        {
            move o = neighbours[j].coord[n];
            size_t w = fwrite(&output_layer[j].weights[o],
                sizeof(double), 1, fp);
            if(w != 1)
            {
                fprintf(stderr, "error: write failed\n");
                exit(EXIT_FAILURE);
            }
            ++written;
        }
    fclose(fp);
    if(nr_of_connections != written)
    {
        fprintf(stderr, "error: mismatch in number of connections written\n");
        exit(EXIT_FAILURE);
    }
}

static void populate_desired_output(
    const training_example * te
){
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        desired_output[i] = TARGET_VALUE_OFFSET - HYPERBOLIC_CONSTANT_A;
    desired_output[te->m] = HYPERBOLIC_CONSTANT_A - TARGET_VALUE_OFFSET;
}

/* the output values are used as accumulator first for performance */
static void forward_pass()
{
    /*
    hidden layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        output_layer[i].output = 0.0;
        hidden_layer[i].output = 0.0;
    }

    /* for each position in layer matrix */
    #pragma omp parallel for schedule(dynamic)
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n = 0; n < neighbours[i].count; ++n)
        {
            move k = neighbours[i].coord[n];
            /* for each input matrix */
            for(u8 p = 0; p < 3; ++p)
                hidden_layer[i].output += input_units[p][k] *
                    hidden_layer[i].weights[p][k];
        }

    /*
    end of hidden layer and start of output layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        hidden_layer[i].output = sigmoid(hidden_layer[i].output);

    /* for each position in layer matrix */
    #pragma omp parallel for schedule(dynamic)
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n = 0; n < neighbours[i].count; ++n)
        {
            move k = neighbours[i].coord[n];
            output_layer[i].output += hidden_layer[k].output *
                output_layer[i].weights[k];
        }

    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        output_layer[i].output = sigmoid(output_layer[i].output);
}

static void init_neurons()
{
    /*
    counting FanIns
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        hidden_layer[i].output = 0.0;
        for(u16 n = 0; n < neighbours[i].count; ++n)
            hidden_layer[i].output += 3.0;
    }
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        output_layer[i].output = 0.0;
        for(u16 n = 0; n < neighbours[i].count; ++n)
            output_layer[i].output += 1.0;
    }

    /*
    hidden layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        double fan_in = hidden_layer[i].output;
        for(u16 n = 0; n < neighbours[i].count; ++n)
        {
            move k = neighbours[i].coord[n];
            for(u8 p = 0; p < 3; ++p)
            {
                do
                    hidden_layer[i].weights[p][k] = (rand_float(4.8 / fan_in) -
                        (2.4 / fan_in)) * 1.0;
                while(fabs(hidden_layer[i].weights[p][k]) < INIT_MIN_WEIGHT);
            }
        }
    }

    /*
    output layer
    */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        double fan_in = output_layer[i].output;
        for(u16 n = 0; n < neighbours[i].count; ++n)
        {
            move k = neighbours[i].coord[n];
            do
                output_layer[i].weights[k] = (rand_float(4.8 / fan_in) -
                    (2.4 / fan_in)) * 1.0;
            while(fabs(output_layer[i].weights[k]) < INIT_MIN_WEIGHT);
        }
    }
}

static void process_output(
    training_example * te,
    u16 * rank
){
    double best_distance = fabs(1.0 - output_layer[te->m].output);
    u16 rk = 1;
    u16 equal_rank = 0;
    bool hit = true;
    u16 legal_plays = 0;
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        if(te->p[i] != NN_ILLEGAL && te->p[i] != NN_BLACK_STONE &&
            te->p[i] != NN_WHITE_STONE)
            ++legal_plays;
        double distance = fabs(1.0 - output_layer[i].output);
        if(distance < best_distance)
        {
            ++rk;
            hit = false;
        }
        else
            if(distance == best_distance)
                ++equal_rank;
    }
    *rank = rk + equal_rank / 2;
    /* if one of the contending best plays was correct */
    if(hit)
        ++hits;
    u16 selected_plays = legal_plays / 4;
    if(*rank <= selected_plays)
        ++selected;
}

static double backward_pass()
{
    double sum_quadratic_error = 0.0;
    // Output layer
    #pragma omp parallel for schedule(dynamic)
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        double desired = desired_output[i];
        double observed = output_layer[i].output;
        output_layer[i].local_gradient = (HYPERBOLIC_CONSTANT_B /
            HYPERBOLIC_CONSTANT_A) * (desired - observed) *
            (HYPERBOLIC_CONSTANT_A - observed) * (HYPERBOLIC_CONSTANT_A +
            observed);
        /* error page 161 */
        double error_signal = desired - observed;
        double instantaneous_error = 0.5 * error_signal * error_signal;
        #pragma omp atomic
        sum_quadratic_error += instantaneous_error;
        for(u16 n = 0; n < neighbours[i].count; ++n)
        {
            move k = neighbours[i].coord[n];
            double change = LEARNING_RATE * output_layer[i].local_gradient *
                hidden_layer[k].output;
            output_layer[i].next_weights[k] = output_layer[i].weights[k] +
                change;
        }
    }

    /* Hidden layer */
    #pragma omp parallel for schedule(dynamic)
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
    {
        double observed = hidden_layer[i].output;
        double sum_next_layer_gradients = 0.0;
        for(u16 n = 0; n < neighbours[i].count; ++n)
        {
            move k = neighbours[i].coord[n];
            sum_next_layer_gradients += output_layer[k].local_gradient *
                output_layer[k].weights[i];
            /* now we can finally update the weights of the output neurons */
            output_layer[k].weights[i] = output_layer[k].next_weights[i];
        }
        hidden_layer[i].local_gradient = (HYPERBOLIC_CONSTANT_B /
            HYPERBOLIC_CONSTANT_A) * (HYPERBOLIC_CONSTANT_A - observed) *
            (HYPERBOLIC_CONSTANT_A + observed) * sum_next_layer_gradients;

        for(u16 n = 0; n < neighbours[i].count; ++n)
        {
            move k = neighbours[i].coord[n];
            for(u8 p = 0; p < 3; ++p)
            {
                double change = LEARNING_RATE * hidden_layer[i].local_gradient *
                    input_units[p][k];
                hidden_layer[i].weights[p][k] += change;
            }
        }
    }

    return sum_quadratic_error;
}

static void count_things(
    u32 * nr_of_input_units,
    u32 * nr_of_neurons,
    u32 * nr_of_connections
){
    *nr_of_input_units = TOTAL_BOARD_SIZ * 3;
    *nr_of_neurons = TOTAL_BOARD_SIZ * 2;
    *nr_of_connections = 0;

    /* hidden layer */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u8 l = 0; l < 3; ++l)
            for(u16 n = 0; n < neighbours[i].count; ++n)
                (*nr_of_connections)++;

    /* ouput layer */
    for(move i = 0; i < TOTAL_BOARD_SIZ; ++i)
        for(u16 n = 0; n < neighbours[i].count; ++n)
            (*nr_of_connections)++;
}

static int ranking_comparer(
    const void * a,
    const void * b
){
    u16 * ia = (u16 *)a;
    u16 * ib = (u16 *)b;
    return *ia - *ib;
}

static void process_rankings(
    u16 * val_ranks,
    u32 validation_set_size
){
    qsort(val_ranks, validation_set_size, sizeof(u16), &ranking_comparer);
    u32 sum = 0;
    for(u32 i = 0; i < validation_set_size; ++i)
        sum += val_ranks[i];
    double avg = ((double)sum) / ((double)validation_set_size);
    u16 lowest = val_ranks[0];
    u16 q1 = val_ranks[validation_set_size / 4];
    u16 median = val_ranks[validation_set_size / 2];
    u16 q3 = val_ranks[(validation_set_size / 4) * 3];
    u16 highest = val_ranks[validation_set_size - 1];
    printf(" %5.1f %3u %3u %4u %3u %4u", avg, lowest, q1, median, q3, highest);
}

int main()
{
    rand_init();
    assert_data_folder_exists();
    init_moves_by_distance(neighbours, NN_CONN_DST, true);

    printf("ANN TRAINER\n\tboard size=%dx%d\n\ttarget value offset=%.4f\n\thype\
rbolic constant a=%.4f\n\thyperbolic constant b=%.4f\n\tinitial minimum weight=\
%.4f\n\tlearning rate=%.4f\n", BOARD_SIZ, BOARD_SIZ, TARGET_VALUE_OFFSET,
        HYPERBOLIC_CONSTANT_A, HYPERBOLIC_CONSTANT_B, INIT_MIN_WEIGHT,
        LEARNING_RATE);

    u16 threads;
    #pragma omp parallel
    #pragma omp single
    threads = omp_get_max_threads();
    printf("\tomp thread num=%d\n", threads);

    u32 nr_of_input_units;
    u32 nr_of_neurons;
    count_things(&nr_of_input_units, &nr_of_neurons, &nr_of_connections);
    printf("\tnr of input units=%u\n\tnr of neurons=%u\n\tnr of connections=%u,\
 avg %.1f\n\tmax weight distance=%u\n\n", nr_of_input_units, nr_of_neurons,
        nr_of_connections, ((double)nr_of_connections) /
        ((double)nr_of_neurons), NN_CONN_DST);

    u32 data_set_size = data_set_load2(MAX_TRAINING_SET_SIZE);

    char * s = alloc();
    timestamp(s);
    printf("%s: Init learning rates and randomizing weights\n", s);
    release(s);
    init_neurons();

    u32 training_set_size = (data_set_size / 10) * 9;
    u32 test_set_size = data_set_size - training_set_size;
    u16 * val_ranks = (u16 *)malloc(test_set_size * sizeof(u16));
    if(val_ranks == NULL)
    {
        fprintf(stderr, "error: out of memory exception\n");
        exit(EXIT_FAILURE);
    }

    u32 epoch = 0;
    printf("Epoch       Error  Accuracy  Selected  BPAvg Min  Q1  Med  Q2  Max \
     Time\n");
    while(1)
    {
        hits = selected = 0;
        ++epoch;
        double avg_sq_err = 0.0;
        for(u32 tsi = 0; tsi < training_set_size; ++tsi)
        {
            if((tsi % (training_set_size / 256)) == 0)
            {
                printf("\r %u%%", (tsi * 100) / training_set_size);
                fflush(stdout);
            }
            training_example * te = data_set_get(tsi);
            nn_populate_input_units(input_units, te->p);
            forward_pass();
            populate_desired_output(te);
            double total_instantaneous_error = backward_pass();
            avg_sq_err += total_instantaneous_error;
        }
        printf("\revaluating...");
        fflush(stdout);
        for(u32 vi = 0; vi < test_set_size; ++vi)
        {
            training_example * te = data_set_get(vi + training_set_size);
            nn_populate_input_units(input_units, te->p);
            forward_pass();
            process_output(te, &val_ranks[vi]);
        }
        backup_network(epoch);

        printf("\r%5u %11f %9f %9f ", epoch, avg_sq_err /
            ((double)training_set_size), (((double)hits) /
            ((double)test_set_size)), (((double)selected) /
            ((double)test_set_size)));
        process_rankings(val_ranks, test_set_size);

        s = alloc();
        timestamp(s);
        printf(" %9s\n", s);
        release(s);

        data_set_shuffle(training_set_size);
    }
    return 0;
}


