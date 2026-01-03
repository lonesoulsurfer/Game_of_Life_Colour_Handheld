

Conway's Game of Life - Complete User Guide


Main Menu Navigation
    UP/DOWN: Navigate menu options
    
    B: Select menu item
    
    B: Go back to previous menu


During Gameplay
    UP: Increase speed (faster generations)
    
    DOWN: Decrease speed (slower generations)
    
    LEFT: Change cell size (Tiny → Small → Normal → Large)
    
    RIGHT: ReB/Regenerate current pattern
    
    UP + DOWN (together): Toggle COLOR/MONO mode
    
    B (short press): Return to menu
    
    B (2 second press): Show/hide game rules overlay

    RESET Button (hold 2 seconds): Enter bootloader mode for updates


Edit Mode (Custom games)

	D-Pad: Move cursor
	
	B (short press): Toggle cell on/off
	
	B (2 second press): Start game
	
	B: Return to menu


Main Menu Options
	PRESET - Classic Patterns
	
	Coe Ship - Spaceship that travels across the board
	
	Gosper Glider Gun - Continuously spawns gliders
	
	Diamond - 4-8-12 diamond pattern that evolves
	
	Pulsar - Achim's p144 oscillator (period-144 pattern)
	
	Glider - 56P6H1V0 spaceship pattern


RANDOM - Chaotic Evolution

	Unpredictable patterns emerge
	
	May stabilize into oscillators
	
	May die out completely
	
	Game shows statistics when it stabilizes or dies


SYMMETRIC -  Creates symmetric initial patterns with different sizes

	CUSTOM - Draw Your Own
	
	Step 1: Choose Cell Size
	
	Tiny (2px cells): 160x120 grid
	
	Small (3px cells): 106x80 grid
	
	Normal (4px cells): 80x60 grid
	
	Large (8px cells): 40x30 grid


Step 2: Edit Mode

	Red crosshair shows cursor position
	
	Cursor blinks at 4Hz for visibility
	
	White cells = alive, Black = dead
	
	Move with D-pad, toggle cells with B


Step 3: Run

	Hold B for 2 seconds to start
	
	While running, hold B for 800ms to return to edit mode
	
	Press B to return to menu


ALT GAMES - Alternative Cellular Automata


BRIAN'S BRAIN
	Small: Compact symmetric cluster
	Medium: Larger symmetric pattern (35% density)
	Large: Very large sparse pattern (18% density)
	Random: Scattered center-weighted distribution
	Custom: Draw your own pattern


DAY & NIGHT - Complementary rule B where birth/survival rules mirror each other.
	
	Very stable, creates intricate patterns
	
	Day/Night metaphor in color mode
	
	Often runs indefinitely


SEEDS - "Exploding" automaton where cells live for exactly 1 generation.
	
	Random: Auto-generated 4-way symmetric pattern
	
	Custom: Draw your own (choose cell size first)


CYCLIC CA - Multi-state cellular automaton where cells cycle through 6 states.

	VrtclSym: Vertical mirror symmetry
	
	4WayRot: 4-way rotational symmetry
	
	Random: Random pattern type and size


RULE EXPLORER - Create Custom Rules

	Choose from 9 famous rule variations:

	Conway (B3/S23) - Classic Game of Life

	HighLife (B36/S23) - Like Conway, with replicators

	Maze (B3/S12345) - Creates maze-like patterns

	Coral (B3/S45678) - Grows coral-like structures

	Seeds (B2/S) - Exploding patterns (same as Seeds mode)

	Replicator (B1357/S1357) - Self-replicating patterns

	2x2 (B36/S125) - Stable 2x2 blocks common

	NoDeath (B3/S012345678) - Cells never die once born

	Diamoeba (B35678/S5678) - Diamond-shaped amoebas


Custom Rules - Create your own rules:

	Navigation:
	
	UP/DOWN: Switch between Birth and Survival rows
	
	LEFT/RIGHT: Move cursor (0-8 neighbors)
	
	B (short): Toggle number on/off
	
	B (2 seconds): Start game with custom rules
	
	B: Back to preset menu
			
How It Works:

	Birth row: Select how many neighbors cause birth (0-8)
	
	Survival row: Select how many neighbors keep cell alive (0-8)
	
	Example: Conway's Life is B3 (birth on 3) / S23 (survive on 2 or 3)


Color Modes

All game modes support color! Press UP + DOWN together to toggle.
