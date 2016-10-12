#include "cellular_automata.h"

#include "random.h"

// cellular automata
namespace ca {

const CyclicPreset cyclic_presets[CYCLIC_PRESET_COUNT] = {
    { 1, 3, 3, ca::Neighborhood::Moore       },
    { 1, 2, 4, ca::Neighborhood::Moore       },
    { 1, 3, 4, ca::Neighborhood::Moore       },
    { 2, 2, 6, ca::Neighborhood::Von_Neumann },
    { 3, 4, 5, ca::Neighborhood::Von_Neumann },
    { 3, 5, 8, ca::Neighborhood::Moore       },
};

const BinaryPreset binary_presets[BINARY_PRESET_COUNT] = {
    { FillStyle::Central_Lump, RuleType::Outer_Totalistic, 736,    Neighborhood::Moore       },
    { FillStyle::Just_A_Dot,   RuleType::Outer_Totalistic, 699054, Neighborhood::Moore       },
    { FillStyle::Central_Lump, RuleType::Outer_Totalistic, 494,    Neighborhood::Von_Neumann },
    { FillStyle::Random,       RuleType::Totalistic,       52,     Neighborhood::Von_Neumann },
    { FillStyle::Central_Lump, RuleType::Outer_Totalistic, 510,    Neighborhood::Von_Neumann },
};

const LifePreset life_presets[LIFE_PRESET_COUNT] = {
    { FillStyle::Central_Lump,      {3,4,5,6}, 4, {2},       1, 6  },
    { FillStyle::Central_Lump,      {1,2,3,4}, 4, {3,4},     2, 48 },
    { FillStyle::Central_Lump,      {3,4,5},   3, {2,4},     2, 25 },
    { FillStyle::Central_Lump,      {2,3,4,5}, 4, {1,2,3,4}, 4, 8  },
    { FillStyle::Central_Lump,      {2,3},     2, {2},       1, 8  },
    { FillStyle::Central_Lump,      {2},       1, {2},       1, 25 },
    { FillStyle::Central_Lump,      {2},       1, {1,3},     2, 21 },
    { FillStyle::Central_Lump,      {3,4,6,7}, 4, {2,6,7,8}, 4, 6  },
    { FillStyle::Random_But_Sparse, {1,4,5,6}, 4, {2,3,5,6}, 4, 16 },
    { FillStyle::Random_But_Sparse, {2,3},     2, {2,3},     2, 8  },
    { FillStyle::Random_But_Sparse, {6},       1, {2,4,6},   3, 3  },
    { FillStyle::Random_But_Sparse, {3,5,6},   3, {2,3},     2, 6  },
    { FillStyle::Random,            {3,4,5},   3, {2},       1, 4  },
    { FillStyle::Random_But_Sparse, {3,4,6,7}, 4, {2,5},     2, 6  },
};

void initialise(Grid* grid, int states) {
    grid->columns = 128;
    grid->rows = 128;
    grid->table_index = 0;
    grid->states = states;
}

static void clear(Grid* grid) {
    for (int i = 0; i < grid->columns; ++i) {
        for (int j = 0; j < grid->rows; ++j) {
            grid->cells[grid->table_index][i][j] = 0;
        }
    }
}

// a 2x2 square at the center, or offset upward for odd numbers of rows or columns
void fill_dot_near_center(Grid* grid) {
    clear(grid);
    int i = grid->columns / 2;
    int j = grid->rows / 2;
    int ti = grid->table_index;
    int value = grid->states - 1;
    grid->cells[ti][i  ][j  ] = value;
    grid->cells[ti][i  ][j+1] = value;
    grid->cells[ti][i+1][j  ] = value;
    grid->cells[ti][i+1][j+1] = value;
}

// Just a fixed 5x5 lumpy shape.
void fill_central_lump(Grid* grid) {
    clear(grid);
    int i = grid->columns / 2;
    int j = grid->rows / 2;
    int t = grid->table_index;
    int value = grid->states - 1;
    grid->cells[t][i  ][j-2] = value;
    grid->cells[t][i-1][j-1] = value;
    grid->cells[t][i-2][j-1] = value;
    grid->cells[t][i+1][j-1] = value;
    grid->cells[t][i+2][j-1] = value;
    grid->cells[t][i-1][j+1] = value;
    grid->cells[t][i-2][j+1] = value;
    grid->cells[t][i  ][j+1] = value;
    grid->cells[t][i+2][j+1] = value;
    grid->cells[t][i-2][j+2] = value;
    grid->cells[t][i-1][j+2] = value;
    grid->cells[t][i+2][j+2] = value;
}

void fill_with_randomness(Grid* grid) {
    for (int i = 0; i < grid->columns; ++i) {
        for (int j = 0; j < grid->rows; ++j) {
            grid->cells[grid->table_index][i][j] = random::int_range(0, grid->states - 1);
        }
    }
}

// Randomly chooses whether to try each 4x4 block in the grid, then for each
// of those four 2x2 blocks, randomly chooses whether to fill it. Roughly 25%
// of cells overall are filled.
void fill_with_sparse_randomness(Grid* grid) {
    clear(grid);
    int high = grid->states - 1; // highest valid state
    int ti = grid->table_index;  // table index
    for (int i = 0; i < grid->columns / 4; ++i) {
        for (int j = 0; j < grid->rows / 4; ++j) {
            if (random::generate() & 1) {
                for (int k = 0; k < 2; ++k) {
                    for (int m = 0; m < 2; ++m) {
                        if (random::generate() & 1) {
                            int fi = 4*i + 2*k;
                            int fj = 4*j + 2*m;
                            grid->cells[ti][fi  ][fj  ] = random::int_range(0, high);
                            grid->cells[ti][fi+1][fj  ] = random::int_range(0, high);
                            grid->cells[ti][fi  ][fj+1] = random::int_range(0, high);
                            grid->cells[ti][fi+1][fj+1] = random::int_range(0, high);
                        }
                    }
                }
            }
        }
    }
}

void fill(Grid* grid, FillStyle style) {
    switch (style) {
        case FillStyle::Just_A_Dot:        fill_dot_near_center(grid);        break;
        case FillStyle::Central_Lump:      fill_central_lump(grid);           break;
        case FillStyle::Random:            fill_with_randomness(grid);        break;
        case FillStyle::Random_But_Sparse: fill_with_sparse_randomness(grid); break;
    }
}

// Simulate the automata according to cyclic cellular automata rules, as by
// David Griffeath.
//
// Each cell can take on any of n states. Every generation, the prospective
// next state of a cell is determined by taking (x + 1) % n, where x is the
// current state. The neighboring cells are then checked to see whether
// their state is also this prospective state. If the number of matching
// neighbors exceeds the threshold, the new state of the cell is set to match;
// otherwise, it maintains its current value.
//
// The range is the greatest extent a neighbor cell can be from the center
// along each axis to be considered in the neighborhood. Half the side length
// of a square, rounded up, or half the diagonal of the rhombus, also rounded
// up.
void simulate_cyclic(Grid* grid, Neighborhood neighborhood, int range, int threshold) {
    int tp = grid->table_index;           // previous table index
    int tn = (grid->table_index + 1) % 2; // next table index
    grid->table_index = tn;               // go ahead and flip the tables for next time
    int mi = grid->columns - 1;           // column index mask
    int mj = grid->rows - 1;              // row index mask
    switch (neighborhood) {
        case Neighborhood::Von_Neumann: {
            for (int i = 0; i < grid->columns; ++i) {
                for (int j = 0; j < grid->rows; ++j) {
                    u8 successor = (grid->cells[tp][i][j] + 1) % grid->states; // the cell's next possible state
                    int total = 0;                                             // how many neighboring cells in that state
                    // top half of the diamond, with the middle column
                    for (int x = -range, r = 0; x <= 0; ++x, ++r) {
                        int s = (i + x) & mi;
                        for (int y = -r; y <= r; ++y) {
                            int t = (j + y) & mj;
                            total += grid->cells[tp][s][t] == successor;
                        }
                    }
                    // bottom half of the diamond
                    for (int x = 1, r = range - 1; x <= range; ++x, --r) {
                        int s = (i + x) & mi;
                        for (int y = -r; y <= r; ++y) {
                            int t = (j + y) & mj;
                            total += grid->cells[tp][s][t] == successor;
                        }
                    }
                    // If enough neighbors are in the next state, promote this
                    // cell to their state, otherwise give it the past value.
                    if (total >= threshold) {
                        grid->cells[tn][i][j] = successor;
                    } else {
                        grid->cells[tn][i][j] = grid->cells[tp][i][j];
                    }
                }
            }
            break;
        }
        case Neighborhood::Moore: {
            for (int i = 0; i < grid->columns; ++i) {
                for (int j = 0; j < grid->rows; ++j) {
                    u8 successor = (grid->cells[tp][i][j] + 1) % grid->states; // the cell's next possible state
                    int total = 0;                                             // how many neighboring cells in that state
                    // Check the full square of neighbors, with this cell at the center.
                    for (int x = -range; x <= range; ++x) {
                        int s = (i + x) & mi;
                        for (int y = -range; y <= range; ++y) {
                            int t = (j + y) & mj;
                            total += grid->cells[tp][s][t] == successor;
                        }
                    }
                    // If enough neighbors are in the next state, promote this
                    // cell to their state, otherwise give it the past value.
                    if (total >= threshold) {
                        grid->cells[tn][i][j] = successor;
                    } else {
                        grid->cells[tn][i][j] = grid->cells[tp][i][j];
                    }
                }
            }
            break;
        }
    }
}

// Simulate the automaton as described by the paper Two-Dimensional Cellular
// Automata (1985) by Stephen Wolfram and Norman H. Packard.
//
// Each cell is a 1-bit value and only neighborhoods of cells immediately
// adjacent to a given cell are considered (the range is always 1).
//
// Totalistic rules are specified as a lookup table stored in a 10-bit integer,
// where to get the next state for a cell, the values of the neighboring cells
// are added to the center cell, and that sum is used as the index into the
// table. So the nth bit in the rule is the next state.
//
// Outer-Totalistic rules work such that the rule is a table of nine 2-bit
// pairs where the sum of the neighboring cells is used to look up the pair in
// the table, and the value of the center cell is used to determine which of
// the two within the pair will be the next state.
void simulate_binary(Grid* grid, RuleType rule_type, u32 rule, Neighborhood neighborhood) {
    int tp = grid->table_index;           // previous table index
    int tn = (grid->table_index + 1) % 2; // next table index
    grid->table_index = tn;               // go ahead and flip the tables for next time
    int mi = grid->columns - 1;           // column index mask
    int mj = grid->rows - 1;              // row index mask
    switch (rule_type) {
        case RuleType::Totalistic: {
            switch (neighborhood) {
                case Neighborhood::Moore: {
                    for (int i = 0; i < grid->columns; ++i) {
                        for (int j = 0; j < grid->rows; ++j) {
                            u8 a0 = grid->cells[tp][ i      ][ j      ];
                            u8 a1 = grid->cells[tp][ i      ][(j-1)&mj];
                            u8 a2 = grid->cells[tp][ i      ][(j+1)&mj];
                            u8 a3 = grid->cells[tp][(i-1)&mi][(j-1)&mj];
                            u8 a4 = grid->cells[tp][(i-1)&mi][ j      ];
                            u8 a5 = grid->cells[tp][(i-1)&mi][(j+1)&mj];
                            u8 a6 = grid->cells[tp][(i+1)&mi][(j-1)&mj];
                            u8 a7 = grid->cells[tp][(i+1)&mi][ j      ];
                            u8 a8 = grid->cells[tp][(i+1)&mi][(j+1)&mj];
                            u32 lookup = a0 + a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8;
                            u32 state = (rule & (1 << lookup)) >> lookup;
                            grid->cells[tn][i][j] = state;
                        }
                    }
                    break;
                }
                case Neighborhood::Von_Neumann: {
                    for (int i = 0; i < grid->columns; ++i) {
                        for (int j = 0; j < grid->rows; ++j) {
                            u8 a0 = grid->cells[tp][ i      ][ j      ];
                            u8 a1 = grid->cells[tp][ i      ][(j+1)&mj];
                            u8 a2 = grid->cells[tp][(i+1)&mi][ j      ];
                            u8 a3 = grid->cells[tp][ i      ][(j-1)&mj];
                            u8 a4 = grid->cells[tp][(i-1)&mi][ j      ];
                            u32 lookup = a0 + a1 + a2 + a3 + a4;
                            u32 state = (rule & (1 << lookup)) >> lookup;
                            grid->cells[tn][i][j] = state;
                        }
                    }
                    break;
                }
            }
            break;
        }
        case RuleType::Outer_Totalistic: {
            switch (neighborhood) {
                case Neighborhood::Moore: {
                    for (int i = 0; i < grid->columns; ++i) {
                        for (int j = 0; j < grid->rows; ++j) {
                            u8 a0 = grid->cells[tp][ i      ][ j      ];
                            u8 a1 = grid->cells[tp][ i      ][(j-1)&mj];
                            u8 a2 = grid->cells[tp][ i      ][(j+1)&mj];
                            u8 a3 = grid->cells[tp][(i-1)&mi][(j-1)&mj];
                            u8 a4 = grid->cells[tp][(i-1)&mi][ j      ];
                            u8 a5 = grid->cells[tp][(i-1)&mi][(j+1)&mj];
                            u8 a6 = grid->cells[tp][(i+1)&mi][(j-1)&mj];
                            u8 a7 = grid->cells[tp][(i+1)&mi][ j      ];
                            u8 a8 = grid->cells[tp][(i+1)&mi][(j+1)&mj];
                            u32 lookup = (a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8) << 1 | a0;
                            u32 state = (rule & (1 << lookup)) >> lookup;
                            grid->cells[tn][i][j] = state;
                        }
                    }
                    break;
                }
                case Neighborhood::Von_Neumann: {
                    for (int i = 0; i < grid->columns; ++i) {
                        for (int j = 0; j < grid->rows; ++j) {
                            u8 a0 = grid->cells[tp][ i      ][ j      ];
                            u8 a1 = grid->cells[tp][ i      ][(j+1)&mj];
                            u8 a2 = grid->cells[tp][(i+1)&mi][ j      ];
                            u8 a3 = grid->cells[tp][ i      ][(j-1)&mj];
                            u8 a4 = grid->cells[tp][(i-1)&mi][ j      ];
                            u32 lookup = ((a1 + a2 + a3 + a4) << 1) | a0;
                            u32 state = (rule & (1 << lookup)) >> lookup;
                            grid->cells[tn][i][j] = state;
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
}

void simulate_life(Grid* grid, int* survive, int survive_count, int* born, int born_count) {
    int tp = grid->table_index;           // previous table index
    int tn = (grid->table_index + 1) % 2; // next table index
    grid->table_index = tn;               // go ahead and flip the tables for next time
    int mi = grid->columns - 1;           // column index mask
    int mj = grid->rows - 1;              // row index mask
    int high = grid->states - 1;          // highest valid state
    for (int i = 0; i < grid->columns; ++i) {
        for (int j = 0; j < grid->rows; ++j) {
            int count = 0;
            count += grid->cells[tp][ i      ][(j-1)&mj] == high;
            count += grid->cells[tp][ i      ][(j+1)&mj] == high;
            count += grid->cells[tp][(i-1)&mi][(j-1)&mj] == high;
            count += grid->cells[tp][(i-1)&mi][ j      ] == high;
            count += grid->cells[tp][(i-1)&mi][(j+1)&mj] == high;
            count += grid->cells[tp][(i+1)&mi][(j-1)&mj] == high;
            count += grid->cells[tp][(i+1)&mi][ j      ] == high;
            count += grid->cells[tp][(i+1)&mi][(j+1)&mj] == high;
            if (grid->cells[tp][i][j]) {
                bool matched = false;
                for (int t = 0; t < survive_count; ++t) {
                    if (survive[t] == count) {
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    grid->cells[tn][i][j] = grid->cells[tp][i][j] - 1;
                } else {
                    grid->cells[tn][i][j] = grid->cells[tp][i][j];
                }
            } else {
                bool matched = false;
                for (int t = 0; t < born_count; ++t) {
                    if (born[t] == count) {
                        matched = true;
                        break;
                    }
                }
                if (matched) {
                    grid->cells[tn][i][j] = high;
                } else {
                    grid->cells[tn][i][j] = grid->cells[tp][i][j];
                }
            }
        }
    }
}

} // namespace ca
