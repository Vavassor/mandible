#pragma once

#include "sized_types.h"

// cellular automata
namespace ca {

enum class Neighborhood {
    Von_Neumann, // Diamond-shaped
    Moore,       // Square-shaped
};

enum class RuleType {
    // The sum of the center cell with its neighbors is taken to determine the
    // next state of a cell.
    Totalistic,
    // The next state is determined by a function where the value of the
    // center cell and the sum of its neighbors are considered separately.
    Outer_Totalistic,
};

enum class FillStyle {
    Just_A_Dot,
    Central_Lump,
    Random,
    Random_But_Sparse,
};

struct Grid {
    u8 cells[2][128][128];
    int columns;     // rows and columns must be power-of-two values
    int rows;        // because  i&(rows-1)  is used for wrapping
    int table_index; // which of the tables of cells is the current one
    int states;      // number of possible states a cell can take on
};

void initialise(Grid* grid, int states);
void fill_dot_near_center(Grid* grid);
void fill_central_lump(Grid* grid);
void fill_with_randomness(Grid* grid);
void fill_with_sparse_randomness(Grid* grid);
void fill(Grid* grid, FillStyle style);
void simulate_cyclic(Grid* grid, Neighborhood neighborhood, int range, int threshold);
void simulate_binary(Grid* grid, RuleType rule_type, u32 rule, Neighborhood neighborhood);
void simulate_life(Grid* grid, int* survive, int survive_count, int* born, int born_count);

struct CyclicPreset {
    int range;
    int threshold;
    int states;
    Neighborhood neighborhood;
};

enum CyclicPresetName {
    CYCLIC_PRESET_313,
    CYCLIC_PRESET_IMPERFECT,
    CYCLIC_PRESET_PERFECT,
    CYCLIC_PRESET_SQUARISH_SPIRALS,
    CYCLIC_PRESET_STRIPES,
    CYCLIC_PRESET_CYCLIC_SPIRALS,
    CYCLIC_PRESET_COUNT
};

struct BinaryPreset {
    FillStyle fill_style;
    RuleType rule_type;
    int rule;
    Neighborhood neighborhood;
};

enum BinaryPresetName {
    BINARY_PRESET_FINGERPRINT,
    BINARY_PRESET_SQUARE_TREE,
    BINARY_PRESET_DIAMOND_FLAKE,
    BINARY_PRESET_EATEN_AWAY,
    BINARY_PRESET_DAMP_WIPE,
    BINARY_PRESET_COUNT,
};

struct LifePreset {
    FillStyle fill_style;
    int survive[10];
    int survive_count;
    int born[10];
    int born_count;
    int states;
};

enum LifePresetName {
    LIFE_PRESET_STICKS,
    LIFE_PRESET_THRILLGRILL,
    LIFE_PRESET_BOMBERS,
    LIFE_PRESET_CIRCUIT_GENESIS,
    LIFE_PRESET_COOTIES,
    LIFE_PRESET_FADERS,
    LIFE_PRESET_FIREWORKS,
    LIFE_PRESET_RAKE,
    LIFE_PRESET_XTASY,
    LIFE_PRESET_BELZHAB,
    LIFE_PRESET_BRAIN_6,
    LIFE_PRESET_FROZEN_SPIRALS,
    LIFE_PRESET_STAR_WARS,
    LIFE_PRESET_WORMS,
    LIFE_PRESET_COUNT
};

extern const CyclicPreset cyclic_presets[CYCLIC_PRESET_COUNT];
extern const LifePreset   life_presets[LIFE_PRESET_COUNT];
extern const BinaryPreset binary_presets[BINARY_PRESET_COUNT];

} // namespace ca
