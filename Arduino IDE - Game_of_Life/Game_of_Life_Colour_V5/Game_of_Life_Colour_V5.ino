/*
 * =====================================================================
 *                    CONWAY'S GAME OF LIFE
 *                RP2040 + ST7789V TFT Implementation
 *                COMPLETE FULL SCREEN VERSION WITH VARIABLE CELL SIZE
 *                         NO FLASH UPDATE
 * =====================================================================
 * 
 * Complete rewrite using 2D boolean array for full screen coverage
 * Optimized to only update changed cells - eliminates screen flashing
 * Added variable cell size: Tiny (2px), Small (3px), Normal (4px)
 * 
 * HARDWARE REQUIREMENTS:
 * • Raspberry Pi Pico or RP2040-Zero
 * • ST7789V 2.0" TFT Display (240x320)
 * • 6 buttons on GPIO pins 2, 3, 4, 5, 6, 7
 * 
 * WIRING:
 * ST7789V TFT:
 *   VCC -> 3.3V, GND -> GND
 *   SCL -> GP18 (SPI0 SCK), SDA -> GP19 (SPI0 MOSI)
 *   RES/RST -> GP20, DC -> GP21, CS -> GP17
 * 
 * Buttons (connect between GPIO and GND):
 *   UP -> GP2, DOWN -> GP3, LEFT -> GP4
 *   RIGHT -> GP5, SET -> GP6, B -> GP7
 * 
 * NEW CONTROLS:
 *   During gameplay: LEFT = Change cell size, RIGHT = Reset
 * 
 * Libraries: Adafruit GFX, Adafruit ST7735 and ST7789
 * Board: Raspberry Pi Pico
 * =====================================================================
 */

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <EEPROM.h>

// EEPROM addresses for persistent settings
#define EEPROM_SOUND_ENABLED 0
#define EEPROM_SOUND_VOLUME 1

// TFT Display pins
#define TFT_CS        29
#define TFT_RST       28
#define TFT_DC        27

// SPI1 pins
#define TFT_MOSI      15  // SDA
#define TFT_SCK       14  // SCL

// Display dimensions
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// Maximum board dimensions (for Tiny cell size)
#define MAX_BOARD_WIDTH 160
#define MAX_BOARD_HEIGHT 120

// Button pins
#define BTN_UP_PIN    2
#define BTN_DOWN_PIN  3
#define BTN_LEFT_PIN  5 
#define BTN_RIGHT_PIN 4 
#define BTN_SET_PIN   6
#define BTN_B_PIN     7
#define BTN_RESET_PIN 8 
#define BUZZER_PIN    26

Adafruit_ST7789 display = Adafruit_ST7789(&SPI1, TFT_CS, TFT_DC, TFT_RST);

// Colors
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F

// Cell size settings
int currentCellSize = 4;  // Default: Normal (4px)
int BOARD_WIDTH = 80;     // Will be recalculated based on cell size
int BOARD_HEIGHT = 60;    // Will be recalculated based on cell size

// Game boards - using maximum dimensions
bool board[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
bool newBoard[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];

// Brian's Brain
bool briansBrain_dying[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
bool briansBrain_newDying[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];

// Cyclic CA
uint8_t cyclicStates[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
uint8_t cyclicNewStates[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];

int cyclicThreshold = 3;

// Game variables
uint32_t cellCount = 0;
uint32_t generation = 0;
uint32_t start_t = 0;
uint32_t frame_start_t = 0;
uint32_t max_cellCount = 0;
bool showingStats = false;
unsigned long statsStartTime = 0;
bool colorMode = false;
uint8_t cellAges[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];  // Track how long cells have been alive
#define seedsAges cellAges  // Share arrays - never used simultaneously
unsigned long colorIndicatorTime = 0;
bool showingColorIndicator = false;
unsigned long cellSizeIndicatorTime = 0;
bool showingCellSizeIndicator = false;
bool showingRules = false;
unsigned long lastRuleToggle = 0;

// Rule Explorer variables
bool ruleExplorerMode = false;
uint8_t birthRule = 0b00001000;    // B3 (bit 3 set)
uint8_t survivalRule = 0b00001100; // S23 (bits 2,3 set)
bool inRuleExplorerMenu = false;
uint8_t ruleMenuSelection = 1;
bool inCustomRuleEdit = false;
uint8_t customEditRow = 0; // 0=birth, 1=survival
uint8_t customEditCursor = 3;
bool startedFromCustomRule = false; 

// Preset rules stored in PROGMEM to save RAM
struct Rule {
    const char* name;
    uint8_t birth;
    uint8_t survival;
};

// Store in program memory, not RAM
const Rule PRESET_RULES[] PROGMEM = {
    {"High Life", 0b01001000, 0b00001100},  // B36/S23 - Has replicator
    {"34 Life",   0b00011000, 0b00011000},  // B34/S34 - Crystalline
    {"Diamoeba",  0b11111000, 0b11111000},  // B35678/S5678 - Organic blobs
    {"Replicator",0b10101010, 0b10101010},  // B1357/S1357 - Self-copies
    {"Long Life", 0b00001000, 0b00100000},  // B3/S5 - Stable structures
    {"Maze",      0b00001000, 0b00111110},  // B3/S12345 - Maze patterns
    {"Coral",     0b00001000, 0b11111000},  // B3/S45678 - Coral growth
    {"2x2",       0b01001000, 0b00100010},  // B36/S125 - 2x2 blocks
    {"Dry Life",  0b10001000, 0b00001100},  // B37/S23 - Explodes slowly
    {"Amoeba",    0b10101000, 0b10101010},  // B357/S1357 - Chaotic blobs
    {"Coagulate", 0b10001000, 0b00001100},  // B37/S23 - Slow expand
    {"Gnarl",     0b00000010, 0b00000010},  // B1/S1 - Chaotic trails
};
const int NUM_PRESET_RULES = 12;

// Control variables
unsigned long last_button_check_time = 0;
uint8_t currentSpeedIndex = 0;
bool buttonProcessingInProgress = false;
uint8_t cursorX = 0;
uint8_t cursorY = 0;
bool editMode = true;
bool inMenuMode = true;
uint8_t menuSelection = 1;
uint8_t currentSymmetryType = 0;
uint8_t currentSymmetrySize = 0;
uint8_t dayNightDensitySelection = 1;

// Game modes
bool inPatternsSubMenu = false;
uint8_t patternSelection = 1;
uint8_t gameMode = 1;
const int frameDelays[] = {500, 400, 300, 250, 200, 150, 100, 75, 50, 40, 30, 25};
const int numSpeeds = sizeof(frameDelays) / sizeof(frameDelays[0]);
uint8_t currentBriansBrainSize = 1;

// Submenu variables
bool inSymmetricSubMenu = false;
uint8_t symmetricSizeSelection = 1;
bool inAltGamesSubMenu = false;
uint8_t altGameSelection = 1;
bool inSeedsSubMenu = false;
uint8_t seedsGameSelection = 1;
bool seedsWasCustom = false;
bool inDayNightSubMenu = false;
bool inCellSizeSubMenu = false;
uint8_t cellSizeSelection = 3;  // 1=Tiny, 2=Small, 3=Normal
uint8_t pendingGameMode = 0;     // Store which game mode triggered cell size menu
bool inToolsSubMenu = false;
uint8_t toolsSelection = 1;

// Tool settings
bool showGridLines = false;
bool showPopCounter = false;
bool trailMode = false;

// Game state
bool briansBrainMode = false;
bool inBriansBrainSubMenu = false;
uint8_t briansBrainSizeSelection = 1;
bool briansBrainWasCustom = false;
int briansBrain_lowCellCounter = 0;
bool briansBrainColorMode = false;

bool dayNightMode = false;
bool dayNightColorMode = false;
bool cyclicMode = false;
bool inCyclicSubMenu = false;
uint8_t cyclicSizeSelection = 1;
uint8_t currentCyclicSize = 1;
bool cyclicColorMode = false;
bool seedsMode = false;
bool seedsColorMode = false;

// Sound settings
bool soundEnabled = true;
uint8_t soundVolume = 2;  // 0=Off, 1=Low, 2=Medium, 3=High

// World topology (applies to all games)
bool toroidalWorld = true;  // true = wraparound edges, false = open edges

// Function prototypes
void updateBoardDimensions();
void randomizeBoard();
void drawCell(int x, int y, uint16_t color);
void collectRandomness(int ms);
void handleDirectionalButtons();
void handleSetButton();
void handleButtonB();
void handleRuleExplorerButtons();
void playMenuBeep();
bool isBtnReset();
void drawCursor();
void clearBoard();
void showMenu();
void showGameStatistics();
void startShowingStats();
void loadPresetPattern(int patternId);
void generateSymmetricPattern(int symmetryType);
void generateVerticalSymmetric();
void generateHorizontalSymmetric();
void generateRotationalSymmetric();
void generateSmallSymmetricPattern(int symmetryType);
void generateMediumSymmetricPattern(int symmetryType);
void updateDayNight();
void drawDayNight();
void initializeDayNight();
void updateCyclic();
void drawCyclic();
void initializeCyclic();
void updateSeeds();
void drawSeeds();
void initializeSeeds();
void updateBriansBrain();
void drawBriansBrain();
void toggleCellAtCursor();
void showGameRules();
void generateBriansBrainSmall();
void generateBriansBrainMedium();
void generateBriansBrainLarge();
void generateBriansBrainRandom();
void updateGame();
void drawBoard();
void changeCellSize();
void showCellSizeIndicator();
uint16_t getColorByAge(uint8_t age);
void drawRuleIndicator();
void showRuleExplorerMenu();

// Button functions
bool isBtnUp() { return digitalRead(BTN_UP_PIN) == LOW; }
bool isBtnDown() { return digitalRead(BTN_DOWN_PIN) == LOW; }
bool isBtnLeft() { return digitalRead(BTN_LEFT_PIN) == LOW; }
bool isBtnRight() { return digitalRead(BTN_RIGHT_PIN) == LOW; }
bool isBtnSet() { return digitalRead(BTN_SET_PIN) == LOW; }
bool isBtnB() { return digitalRead(BTN_B_PIN) == LOW; }
bool isBtnReset() { return digitalRead(BTN_RESET_PIN) == LOW; }

void resetDrawFlags() {
    // Forces all draw functions to redraw from scratch
    // This is a workaround - the actual fix is in each draw function
}

void updateBoardDimensions() {
    BOARD_WIDTH = SCREEN_WIDTH / currentCellSize;
    BOARD_HEIGHT = SCREEN_HEIGHT / currentCellSize;
    cursorX = BOARD_WIDTH / 2;
    cursorY = BOARD_HEIGHT / 2;
}

void showCellSizeIndicator() {
    // Show a brief indicator of the new cell size
    display.fillRect(SCREEN_WIDTH - 60, 5, 55, 20, COLOR_BLACK);
    display.setTextSize(1);
    display.setTextColor(COLOR_WHITE);
    display.setCursor(SCREEN_WIDTH - 58, 10);
    
    switch(currentCellSize) {
        case 2: display.print("Tiny"); break;
        case 3: display.print("Small"); break;
        case 4: display.print("Normal"); break;
        case 8: display.print("Large"); break;
    }
    
    // Set timer for indicator
    cellSizeIndicatorTime = millis();
    showingCellSizeIndicator = true;
}

void changeCellSize() {
    // Cycle through cell sizes: 2 -> 3 -> 4 -> 12 -> 2
    if (currentCellSize == 2) {
        currentCellSize = 3;  // Tiny -> Small
    } else if (currentCellSize == 3) {
        currentCellSize = 4;  // Small -> Normal
    } else if (currentCellSize == 4) {
        currentCellSize = 8;  // Normal -> Large
    } else {
        currentCellSize = 2;  // Large -> Tiny
    }
    
    updateBoardDimensions();
    
    // Clear and reinitialize based on current mode
    display.fillScreen(COLOR_BLACK);
    
    if (gameMode == 1 || ruleExplorerMode) {
        // Random game
        if (showGridLines) {
            drawFullGrid();
        }
        randomizeBoard();
        generation = 0;
        cellCount = 0;
        max_cellCount = 0;
    } else if (gameMode == 2) {
        // Custom game - stay in edit mode
        if (showGridLines) {
            drawFullGrid();
        }
        clearBoard();
    } else if (gameMode == 3) {
        // Preset patterns - reload current pattern
        if (showGridLines) {
            drawFullGrid();
        }
        loadPresetPattern(patternSelection);
    } else if (gameMode == 4) {
        // Symmetric game
        if (showGridLines) {
            drawFullGrid();
        }
        clearBoard();
        switch(currentSymmetrySize) {
            case 1: generateSmallSymmetricPattern(currentSymmetryType); break;
            case 2: generateMediumSymmetricPattern(currentSymmetryType); break;
            case 3: generateSymmetricPattern(currentSymmetryType); break;
        }
    } else if (gameMode == 5 && dayNightMode) {
        // Day & Night
        if (showGridLines) {
            drawFullGrid();
        }
        initializeDayNight();
    } else if (gameMode == 6 && seedsMode) {
        // Seeds
        if (showGridLines) {
            drawFullGrid();
        }
        if (seedsWasCustom) {
            clearBoard();
        } else {
            initializeSeeds();
        }
   } else if (gameMode == 8 && briansBrainMode) {
        // Brian's Brain
        if (showGridLines) {
            drawFullGrid();
        }
        if (briansBrainWasCustom) {
            clearBoard();
        } else {
            clearBoard();
            switch(currentBriansBrainSize) {
                case 1: generateBriansBrainSmall(); break;
                case 2: generateBriansBrainMedium(); break;
                case 3: generateBriansBrainLarge(); break;
                case 4: generateBriansBrainRandom(); break;
            }
        }
        drawBriansBrain();
    } else if (gameMode == 9 && cyclicMode) {
        // Cyclic CA
        if (showGridLines) {
            drawFullGrid();
        }
        initializeCyclic();
    }
    
    generation = 0;
    start_t = micros();
    frame_start_t = millis();
    
    showCellSizeIndicator();
    playCellSizeChange();
}

void toggleCellAtCursor() {
    board[cursorX][cursorY] = !board[cursorX][cursorY];
}

void drawCursor() {
    static bool lastBlinkState = false;
    static unsigned long lastBlinkUpdate = 0;
    
    bool cellIsLit = board[cursorX][cursorY];
    bool blinkState = (millis() / 125) % 2;  // 125ms = 4 flashes per second
    
    // Only update display when blink state actually changes
    if (blinkState != lastBlinkState || (millis() - lastBlinkUpdate > 100)) {
        lastBlinkState = blinkState;
        lastBlinkUpdate = millis();
        
        // FIRST: Always draw all cells in their actual state (no flashing)
        // Center cell
        display.fillRect(cursorX * currentCellSize, cursorY * currentCellSize, currentCellSize, currentCellSize, 
                        cellIsLit ? COLOR_WHITE : COLOR_BLACK);
        
        // Left cell
        if (cursorX > 0) {
            bool leftCellLit = board[cursorX-1][cursorY];
            display.fillRect((cursorX-1) * currentCellSize, cursorY * currentCellSize, currentCellSize, currentCellSize, 
                            leftCellLit ? COLOR_WHITE : COLOR_BLACK);
        }
        
        // Right cell
        if (cursorX < BOARD_WIDTH-1) {
            bool rightCellLit = board[cursorX+1][cursorY];
            display.fillRect((cursorX+1) * currentCellSize, cursorY * currentCellSize, currentCellSize, currentCellSize, 
                            rightCellLit ? COLOR_WHITE : COLOR_BLACK);
        }
        
        // Top cell
        if (cursorY > 0) {
            bool topCellLit = board[cursorX][cursorY-1];
            display.fillRect(cursorX * currentCellSize, (cursorY-1) * currentCellSize, currentCellSize, currentCellSize, 
                            topCellLit ? COLOR_WHITE : COLOR_BLACK);
        }
        
        // Bottom cell
        if (cursorY < BOARD_HEIGHT-1) {
            bool bottomCellLit = board[cursorX][cursorY+1];
            display.fillRect(cursorX * currentCellSize, (cursorY+1) * currentCellSize, currentCellSize, currentCellSize, 
                            bottomCellLit ? COLOR_WHITE : COLOR_BLACK);
        }
        
        // THEN: If blink is ON, draw red crosshair ON TOP of the cells
        if (blinkState) {
            if (cursorX > 0) 
                display.fillRect((cursorX-1) * currentCellSize, cursorY * currentCellSize, currentCellSize, currentCellSize, COLOR_RED);
            if (cursorX < BOARD_WIDTH-1) 
                display.fillRect((cursorX+1) * currentCellSize, cursorY * currentCellSize, currentCellSize, currentCellSize, COLOR_RED);
            if (cursorY > 0) 
                display.fillRect(cursorX * currentCellSize, (cursorY-1) * currentCellSize, currentCellSize, currentCellSize, COLOR_RED);
            if (cursorY < BOARD_HEIGHT-1) 
                display.fillRect(cursorX * currentCellSize, (cursorY+1) * currentCellSize, currentCellSize, currentCellSize, COLOR_RED);
        }
    }
}

void handleButtonB() {
    static unsigned long lastButtonBPress = 0;
    static bool buttonBWasPressed = false;
    
    // Long press detection for rules
    static unsigned long buttonBPressStart = 0;
    static bool longPressHandled = false;
    
    unsigned long now = millis();
    bool buttonBPressed = isBtnB();
    
    // === GAME MODE (not in menu, not in edit) - Long press for rules ===
    if (!inMenuMode && !editMode) {
        if (buttonBPressed && !buttonBWasPressed) {
            buttonBPressStart = now;
            longPressHandled = false;
            buttonBWasPressed = true;
            return;
        }
        
        if (buttonBPressed && (now - buttonBPressStart >= 800) && !longPressHandled) {
            longPressHandled = true;
            showingRules = !showingRules;
            playMenuBeep();
            
           if (showingRules) {
                showGameRules();
            } else {
                display.fillScreen(COLOR_BLACK);
                if (showGridLines) {
                    drawFullGrid();
                }
                
                if (briansBrainMode) {
                    drawBriansBrain();
                } else if (dayNightMode) {
                    drawDayNight();
                } else if (seedsMode) {
                    drawSeeds();
                } else if (cyclicMode) {
                    drawCyclic();
                } else if (ruleExplorerMode) {
                    // Redraw all cells for Rule Explorer
                    for (int x = 0; x < BOARD_WIDTH; x++) {
                        for (int y = 0; y < BOARD_HEIGHT; y++) {
                            if (board[x][y]) {
                                if (colorMode) {
                                    drawCell(x, y, getColorByAge(cellAges[x][y]));
                                } else {
                                    drawCell(x, y, COLOR_WHITE);
                                }
                            }
                        }
                    }
                    drawRuleIndicator();
                    // Reset frame timer to resume game
                    frame_start_t = millis();
                } else if (gameMode >= 1 && gameMode <= 4) {
                    for (int x = 0; x < BOARD_WIDTH; x++) {
                        for (int y = 0; y < BOARD_HEIGHT; y++) {
                            if (board[x][y]) {
                                if (colorMode) {
                                    drawCell(x, y, getColorByAge(cellAges[x][y]));
                                } else {
                                    drawCell(x, y, COLOR_WHITE);
                                }
                            }
                        }
                    }
                    // Reset frame timer to resume game
                    frame_start_t = millis();
                }
            } 
            return;
        }
        
        // Short press released - go back to menu
        if (!buttonBPressed && buttonBWasPressed) {
            unsigned long pressDuration = now - buttonBPressStart;
            buttonBWasPressed = false;
            
            // Only process as short press if it was truly short (less than 800ms)
            if (pressDuration >= 800 || longPressHandled) {
                longPressHandled = false;
                return;
            }
            
            // If rules are showing, short press does nothing
            if (showingRules) {
                return;
            }
            
            lastButtonBPress = now;
            playMenuBeep();
            
            lastButtonBPress = now;
            playMenuBeep();
            
            // ALWAYS clear screen and board when exiting any game
            display.fillScreen(COLOR_BLACK);
            for (int x = 0; x < BOARD_WIDTH; x++) {
                for (int y = 0; y < BOARD_HEIGHT; y++) {
                    board[x][y] = false;
                    newBoard[x][y] = false;
                    cellAges[x][y] = 0;
                }
            }
            
            // Return to appropriate menu based on current game mode
            if (cyclicMode) {
                cyclicMode = false;
                editMode = false;
                showingStats = false;
                inMenuMode = true;
                inCyclicSubMenu = true;
                cyclicSizeSelection = currentCyclicSize;
                showMenu();
                return;
            }
            
            if (briansBrainMode) {
                briansBrainMode = false;
                editMode = false;
                showingStats = false;
                briansBrain_lowCellCounter = 0;
                inMenuMode = true;
                
                if (briansBrainWasCustom) {
                    inCellSizeSubMenu = true;
                    cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                    pendingGameMode = 8;
                } else {
                    inBriansBrainSubMenu = true;
                    briansBrainSizeSelection = currentBriansBrainSize;
                }
                showMenu();
                return;
            }
            
            if (seedsMode) {
                seedsMode = false;
                editMode = false;
                showingStats = false;
                inMenuMode = true;
                
                if (seedsWasCustom) {
                    inCellSizeSubMenu = true;
                    cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                    pendingGameMode = 6;
                } else {
                    inSeedsSubMenu = true;
                    seedsGameSelection = 1;
                }
                showMenu();
                return;
            }
            
            if (dayNightMode) {
                dayNightMode = false;
                editMode = false;
                showingStats = false;
                inMenuMode = true;
                inDayNightSubMenu = true;
                showMenu();
                return;
            }
            
           if (ruleExplorerMode) {
                display.fillScreen(COLOR_BLACK);
                clearBoard();
                
                ruleExplorerMode = false;
                inMenuMode = true;
                editMode = false;
                showingStats = false;
                inRuleExplorerMenu = true;
                
                if (startedFromCustomRule) {
                    inCustomRuleEdit = true;
                    customEditRow = 0;
                    customEditCursor = 3;
                } else {
                    birthRule = 0b00001000;
                    survivalRule = 0b00001100;
                    ruleMenuSelection = 1;
                }
                
                showRuleExplorerMenu();
                return;
            }
            
            if (gameMode >= 1 && gameMode <= 4) {
                inMenuMode = true;
                editMode = false;
                showingStats = false;
                
                if (gameMode == 3) {
                    inPatternsSubMenu = true;
                    menuSelection = 1;
                } else if (gameMode == 4) {
                    inSymmetricSubMenu = true;
                    symmetricSizeSelection = currentSymmetrySize;
                    menuSelection = 3;
                } else if (gameMode == 2) {
                    inCellSizeSubMenu = true;
                    cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                    pendingGameMode = 2;
                } else {
                    menuSelection = 1;
                }
                
                showMenu();
                return;
            }
        }
        
        // Reset flag on button release after long press
        if (!buttonBPressed && buttonBWasPressed && longPressHandled) {
            buttonBWasPressed = false;
            return;
        }
        
        if (buttonBPressed) {
            return;
        }
        
        return; // In game mode, nothing else to do
    }
    
    // === MENU/EDIT MODE - Short press to go back, but also track for potential long press ===
    
    // Debounce
    if (now - lastButtonBPress < 200) return;
    
    // On button press - record it and start timing
    if (buttonBPressed && !buttonBWasPressed) {
        buttonBWasPressed = true;
        buttonBPressStart = now;
        longPressHandled = false;
        return;
    }
    
    // Check for long press in menu/edit mode (just ignore it, don't trigger back action)
    if (buttonBPressed && buttonBWasPressed && !longPressHandled) {
        unsigned long pressDuration = now - buttonBPressStart;
        if (pressDuration >= 800) {
            longPressHandled = true;
            // In menu mode, long press does nothing - just prevent short press action
            return;
        }
    }
    
    // On button release - execute action only if it was a short press
    if (!buttonBPressed && buttonBWasPressed) {
        buttonBWasPressed = false;
        
        // If it was a long press, don't do the back action
        if (longPressHandled) {
            longPressHandled = false;
            return;
        }
        
        lastButtonBPress = now;
        playMenuBeep();

        if (inCellSizeSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inCellSizeSubMenu = false;
            if (pendingGameMode == 2) {
                menuSelection = 4;
            } else if (pendingGameMode == 6) {
                inSeedsSubMenu = true;
                seedsGameSelection = 2;
            } else if (pendingGameMode == 8) {
                inBriansBrainSubMenu = true;
                briansBrainSizeSelection = 5;
            }
            pendingGameMode = 0;
            showMenu();
            return;
        }

        if (inBriansBrainSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inBriansBrainSubMenu = false;
            inAltGamesSubMenu = true;
            altGameSelection = 1;
            showMenu();
            return;
        }
        
        if (inSeedsSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inSeedsSubMenu = false;
            inAltGamesSubMenu = true;
            altGameSelection = 3;
            showMenu();
            return;
        }
        
        if (inDayNightSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inDayNightSubMenu = false;
            inAltGamesSubMenu = true;
            altGameSelection = 2;
            showMenu();
            return;
        }
        
        if (inCyclicSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inCyclicSubMenu = false;
            inAltGamesSubMenu = true;
            altGameSelection = 4;
            showMenu();
            return;
        }
        
        if (inSymmetricSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inSymmetricSubMenu = false;
            menuSelection = 3;
            showMenu();
            return;
        }

        if (inToolsSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inToolsSubMenu = false;
            menuSelection = 7;  // Back to Tools position
            showMenu();
            return;
        }
        
        if (inPatternsSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inPatternsSubMenu = false;
            menuSelection = 1;
            showMenu();
            return;
        }
        
        if (inAltGamesSubMenu) {
            display.fillScreen(COLOR_BLACK);
            inAltGamesSubMenu = false;
            menuSelection = 6;  // Alt Games is now position 6
            showMenu();
            return;
        }
        
        if (cyclicMode && !inMenuMode) {
            cyclicMode = false;
            editMode = false;
            showingStats = false;
            inMenuMode = true;
            inCyclicSubMenu = true;
            cyclicSizeSelection = currentCyclicSize;
            showMenu();
            return;
        }
        
        if (briansBrainMode && !inMenuMode) {
            briansBrainMode = false;
            editMode = false;
            showingStats = false;
            briansBrain_lowCellCounter = 0;
            inMenuMode = true;
            
            if (briansBrainWasCustom) {
                inCellSizeSubMenu = true;
                cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                pendingGameMode = 8;
            } else {
                inBriansBrainSubMenu = true;
                briansBrainSizeSelection = currentBriansBrainSize;
            }
            showMenu();
            return;
        }
        
        if (seedsMode && !inMenuMode) {
            seedsMode = false;
            editMode = false;
            showingStats = false;
            inMenuMode = true;
            
            if (seedsWasCustom) {
                inCellSizeSubMenu = true;
                cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                pendingGameMode = 6;
            } else {
                inSeedsSubMenu = true;
                seedsGameSelection = 1;
            }
            showMenu();
            return;
        }
        
        if (dayNightMode && !inMenuMode) {
            dayNightMode = false;
            editMode = false;
            showingStats = false;
            inMenuMode = true;
            inDayNightSubMenu = true;
            showMenu();
            return;
        }
        
         if (ruleExplorerMode && !inMenuMode) {
            ruleExplorerMode = false;
            editMode = false;
            showingStats = false;
            
            display.fillScreen(COLOR_BLACK);
            delay(200);
            
            clearBoard();
            
            inMenuMode = true;
            inRuleExplorerMenu = true;
            
            if (startedFromCustomRule) {
                inCustomRuleEdit = true;
                customEditRow = 0;
                customEditCursor = 3;
            } else {
                birthRule = 0b00001000;
                survivalRule = 0b00001100;
                ruleMenuSelection = 1;
            }
            
            showRuleExplorerMenu();
            return;
        }
        
        if (!inMenuMode && (gameMode == 1 || gameMode == 2 || gameMode == 3 || gameMode == 4)) {
            inMenuMode = true;
            editMode = false;
            showingStats = false;
            
            if (gameMode == 3) {
                inPatternsSubMenu = true;
                menuSelection = 1;
            } else if (gameMode == 4) {
                inSymmetricSubMenu = true;
                symmetricSizeSelection = currentSymmetrySize;
                menuSelection = 3;
            } else if (gameMode == 2) {
                inCellSizeSubMenu = true;
                cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                pendingGameMode = 2;
            } else {
                menuSelection = 1;
            }
            
            showMenu();
            return;
        }
    }
}

void handleSetButton() {
    unsigned long now = millis();
    static unsigned long lastSetPress = 0;
    static unsigned long setButtonPressStart = 0;
    static bool setButtonPressed = false;
    static bool longPressActivated = false;
    static bool justEnteredSubmenu = false;
    static unsigned long submenuEntryTime = 0;
    
    bool currentlyPressed = isBtnSet();
    
    if (now - lastSetPress < 50) return;
    
    if (currentlyPressed && !setButtonPressed) {
        setButtonPressed = true;
        setButtonPressStart = now;
        lastSetPress = now;
        longPressActivated = false;
        return;
    }

    // Add debouncing for short press detection
    if (now - lastSetPress < 50) return;
    
    if (currentlyPressed && setButtonPressed && !longPressActivated) {
        unsigned long pressDuration = now - setButtonPressStart;
        
        if (editMode && !inMenuMode && pressDuration >= 800) {
            longPressActivated = true;
            editMode = false;
            generation = 0;
            currentSpeedIndex = 0;  // Reset to slowest speed
            start_t = micros();
            frame_start_t = millis();
            playGameStart();  // REPLACE flashLED() with this
            
            if (gameMode == 6) {
                seedsMode = true;
            }
            if (gameMode == 9) {
                cyclicMode = true;
            }
            return;
        }
        
        if (!editMode && !inMenuMode && pressDuration >= 800) {
            if (gameMode == 2 || (gameMode == 6 && seedsWasCustom) || (gameMode == 8 && briansBrainWasCustom)) {
                longPressActivated = true;
                editMode = true;
                playMenuBeep();
                return;
            }
        }
    }
    
    if (!currentlyPressed && setButtonPressed) {
        unsigned long pressDuration = now - setButtonPressStart;
        setButtonPressed = false;
        
        if (longPressActivated) {
            longPressActivated = false;
            return;
        }
        
        if (editMode && !inMenuMode && pressDuration < 800) {
            // Add small delay to ensure clean press/release
            delay(10);
            toggleCellAtCursor();
            playMenuBeep();
            
            // Force redraw of cursor area
            display.fillScreen(COLOR_BLACK);
            for (int x = 0; x < BOARD_WIDTH; x++) {
                for (int y = 0; y < BOARD_HEIGHT; y++) {
                    if (board[x][y]) {
                        drawCell(x, y, COLOR_WHITE);
                    }
                }
            }
            return;
        }
        
        if (inMenuMode && pressDuration < 800) {
            if (buttonProcessingInProgress) return;
            buttonProcessingInProgress = true;
            
            if (!inPatternsSubMenu && !inSymmetricSubMenu && !inAltGamesSubMenu && 
                !inBriansBrainSubMenu && !inSeedsSubMenu && !inCyclicSubMenu && !inDayNightSubMenu && !inCellSizeSubMenu && !inToolsSubMenu) {
                
                switch(menuSelection) {
                    case 1:
                        inPatternsSubMenu = true;
                        patternSelection = 1;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                        
                    case 2:
                        gameMode = 1;
                        display.fillScreen(COLOR_BLACK);
                        if (showGridLines) {
                            drawFullGrid();
                        }
                        randomizeBoard();
                        editMode = false;
                        inMenuMode = false;
                        generation = 0;
                        start_t = micros();
                        frame_start_t = millis();
                        playGameStart();
                        buttonProcessingInProgress = false;
                        break;
                        
                    case 3:
                        inSymmetricSubMenu = true;
                        symmetricSizeSelection = 1;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                        
                    case 4:
                        // Custom - show cell size submenu first
                        inCellSizeSubMenu = true;
                        cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                        pendingGameMode = 2;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                        
                    case 5:  // Rule Explorer
                        inRuleExplorerMenu = true;
                        ruleMenuSelection = 1;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showRuleExplorerMenu();
                        break;
                        
                    case 6:  // Alt Games
                        inAltGamesSubMenu = true;
                        altGameSelection = 1;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                        
                  case 7:  // Tools
                        inToolsSubMenu = true;
                        toolsSelection = 1;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;  
                }
                return;
            }
            
            if (justEnteredSubmenu && (now - submenuEntryTime < 300)) {
                return;
            }
            justEnteredSubmenu = false;
            
            if (inPatternsSubMenu) {
                gameMode = 3;
                display.fillScreen(COLOR_BLACK);
                if (showGridLines) {
                    drawFullGrid();
                }
                loadPresetPattern(patternSelection);
                inMenuMode = false;
                inPatternsSubMenu = false;
                editMode = false;
                generation = 0;
                start_t = micros();
                frame_start_t = millis();
                playGameStart();
                buttonProcessingInProgress = false;
                return;
            }
            
            if (inSymmetricSubMenu) {
                gameMode = 4;
                currentSymmetrySize = symmetricSizeSelection;
                currentSymmetryType = random(3) + 1;
                
                clearBoard();
                switch(currentSymmetrySize) {
                    case 1: generateSmallSymmetricPattern(currentSymmetryType); break;
                    case 2: generateMediumSymmetricPattern(currentSymmetryType); break;
                    case 3: generateSymmetricPattern(currentSymmetryType); break;
                }
                
                inMenuMode = false;
                inSymmetricSubMenu = false;
                editMode = false;
                generation = 0;
                start_t = micros();
                frame_start_t = millis();
                playGameStart();  
                buttonProcessingInProgress = false;
                
                display.fillScreen(COLOR_BLACK);
                if (showGridLines) {
                    drawFullGrid();
                }
                drawBoard();
                return;
            }
            
            if (inAltGamesSubMenu) {
                switch(altGameSelection) {
                    case 1:
                        inBriansBrainSubMenu = true;
                        briansBrainSizeSelection = 1;
                        inAltGamesSubMenu = false;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                    case 2:
                        inDayNightSubMenu = true;
                        dayNightDensitySelection = 1;
                        inAltGamesSubMenu = false;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                    case 3:
                        inSeedsSubMenu = true;
                        seedsGameSelection = 1;
                        inAltGamesSubMenu = false;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                    case 4:
                        inCyclicSubMenu = true;
                        cyclicSizeSelection = 1;
                        inAltGamesSubMenu = false;
                        justEnteredSubmenu = true;
                        submenuEntryTime = now;
                        showMenu();
                        break;
                }
                return;
            }
            
            if (inBriansBrainSubMenu) {
                gameMode = 8;
                briansBrainMode = true;
                currentBriansBrainSize = briansBrainSizeSelection;
                
                if (briansBrainSizeSelection == 5) {
                    // Custom - show cell size menu
                    briansBrainWasCustom = true;
                    inBriansBrainSubMenu = false;
                    inCellSizeSubMenu = true;
                    cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                    pendingGameMode = 8;
                    justEnteredSubmenu = true;
                    submenuEntryTime = now;
                    showMenu();
                    return;
                }
                
                briansBrainWasCustom = false;
                inBriansBrainSubMenu = false;
                inMenuMode = false;
                editMode = false;
                clearBoard();
                
                int symmetryType = random(3) + 1;
                switch(briansBrainSizeSelection) {
                    case 1: generateBriansBrainSmall(); break;
                    case 2: generateBriansBrainMedium(); break;
                    case 3: generateBriansBrainLarge(); break;
                    case 4: generateBriansBrainRandom(); break;
                }
                
                generation = 0;
                frame_start_t = now;
                start_t = micros();
                playGameStart(); 
                buttonProcessingInProgress = false;
                drawBriansBrain();
                
                if (showGridLines) {
                    drawFullGrid();
                }
                return;
            }
            
            if (inDayNightSubMenu) {
                gameMode = 5;
                dayNightMode = true;
                initializeDayNight();
                
                inDayNightSubMenu = false;
                inMenuMode = false;
                editMode = false;
                generation = 0;
                frame_start_t = now;
                start_t = micros();
                playGameStart(); 
                buttonProcessingInProgress = false;
                
                if (showGridLines) {
                    drawFullGrid();
                }
                return;
            }
            
            if (inSeedsSubMenu) {
                gameMode = 6;
                seedsMode = true;
                
                 if (seedsGameSelection == 1) {
                    // Random Seeds
                    seedsWasCustom = false;
                    initializeSeeds();
                    
                    editMode = false;
                    inSeedsSubMenu = false;
                    inMenuMode = false;
                    generation = 0;
                    start_t = micros();
                    frame_start_t = now;
                    playGameStart(); 
                    buttonProcessingInProgress = false;
                    
                    if (showGridLines) {
                        drawFullGrid();
                    }
                }else {
                    // Custom Seeds - show cell size menu
                    inSeedsSubMenu = false;
                    inCellSizeSubMenu = true;
                    cellSizeSelection = (currentCellSize == 2) ? 1 : ((currentCellSize == 3) ? 2 : ((currentCellSize == 8) ? 4 : 3));
                    pendingGameMode = 6;
                    justEnteredSubmenu = true;
                    submenuEntryTime = now;
                    showMenu();
                }
                return;
            }
            
            if (inCyclicSubMenu) {
                gameMode = 9;
                cyclicMode = true;
                currentCyclicSize = cyclicSizeSelection;
                
                initializeCyclic();
                
                inCyclicSubMenu = false;
                inMenuMode = false;
                editMode = false;
                generation = 0;
                frame_start_t = now;
                start_t = micros();
                playGameStart(); 
                buttonProcessingInProgress = false;
                drawCyclic();
                
                if (showGridLines) {
                    drawFullGrid();
                }
                return;
            }

            if (inToolsSubMenu) {
                // Toggle the selected tool option
                if (toolsSelection == 1) {
                    soundEnabled = !soundEnabled;
                    EEPROM.write(EEPROM_SOUND_ENABLED, soundEnabled);
                    EEPROM.commit();
                    playMenuBeep();
                    showMenu();
                    buttonProcessingInProgress = false;
                    delay(200);
                    return;
                } else if (toolsSelection == 2) {
                    // Cycle through volume levels: Low -> Medium -> High -> Low
                    soundVolume++;
                    if (soundVolume > 3) soundVolume = 1;
                    EEPROM.write(EEPROM_SOUND_VOLUME, soundVolume);
                    EEPROM.commit();
                    playMenuBeep();
                    showMenu();
                    buttonProcessingInProgress = false;
                    delay(200);
                    return;
                } else if (toolsSelection == 3) {
                    toroidalWorld = !toroidalWorld;
                    playMenuBeep();
                    showMenu();
                    buttonProcessingInProgress = false;
                    delay(200);
                    return;
                } else if (toolsSelection == 4) {
                    showGridLines = !showGridLines;
                    playMenuBeep();
                    
                    // If we're in a game (not menu/edit), redraw with grid
                    if (!inMenuMode && !editMode) {
                        display.fillScreen(COLOR_BLACK);
                        if (showGridLines) {
                            drawFullGrid();
                        }
                        // Redraw all cells
                        for (int x = 0; x < BOARD_WIDTH; x++) {
                            for (int y = 0; y < BOARD_HEIGHT; y++) {
                                if (board[x][y]) {
                                    if (colorMode && gameMode >= 1 && gameMode <= 4) {
                                        drawCell(x, y, getColorByAge(cellAges[x][y]));
                                    } else {
                                        drawCell(x, y, COLOR_WHITE);
                                    }
                                }
                            }
                        }
                    } else {
                        showMenu();
                    }
                    
                    buttonProcessingInProgress = false;
                    delay(200);
                    return;
                }else if (toolsSelection == 5) {
                    showPopCounter = !showPopCounter;
                    playMenuBeep();
                    showMenu();
                    buttonProcessingInProgress = false;
                    delay(200);
                    return;
                } else if (toolsSelection == 6) {
                    trailMode = !trailMode;
                    playMenuBeep();
                    showMenu();
                    buttonProcessingInProgress = false;
                    delay(200);
                    return;
                }
            }
            
            if (inCellSizeSubMenu) {
                // Apply cell size selection
                switch(cellSizeSelection) {
                    case 1: currentCellSize = 2; break;  // Tiny
                    case 2: currentCellSize = 3; break;  // Small
                    case 3: currentCellSize = 4; break;  // Normal
                    case 4: currentCellSize = 8; break; // Large
                }
                updateBoardDimensions();
                
                // Now proceed to the game that requested cell size selection
                if (pendingGameMode == 2) {
                    // Regular Custom game
                    gameMode = 2;
                    clearBoard();
                    cursorX = BOARD_WIDTH / 2;
                    cursorY = BOARD_HEIGHT / 2;
                    editMode = true;
                    inMenuMode = false;
                    generation = 0;
                    currentSpeedIndex = 0;  // Reset to slowest speed
                } else if (pendingGameMode == 6) {
                    // Seeds Custom
                    gameMode = 6;
                    seedsMode = true;
                    seedsWasCustom = true;
                    clearBoard();
                    cursorX = BOARD_WIDTH / 2;
                    cursorY = BOARD_HEIGHT / 2;
                    editMode = true;
                    inMenuMode = false;
                    generation = 0;
                    currentSpeedIndex = 0;  // Reset to slowest speed
                    start_t = micros();
                    frame_start_t = now;
                } else if (pendingGameMode == 8) {
                    // Brian's Brain Custom
                    gameMode = 8;
                    briansBrainMode = true;
                    briansBrainWasCustom = true;
                    clearBoard();
                    cursorX = BOARD_WIDTH / 2;
                    cursorY = BOARD_HEIGHT / 2;
                    editMode = true;
                    inMenuMode = false;
                    generation = 0;
                    currentSpeedIndex = 0;  // Reset to slowest speed
                }
                
                inCellSizeSubMenu = false;
                pendingGameMode = 0;
                buttonProcessingInProgress = false;
                return;
            }
        }
    }
}
void showGameStatistics() {
    display.fillScreen(COLOR_BLACK);
    display.setTextSize(4);
    display.setCursor(40, 60);
    display.setTextColor(COLOR_WHITE);
    display.println("GAME OVER");
    
    display.setTextSize(2);
    
    // Calculate text width for centering
    // "Generations: " is 13 chars, plus up to 10 digits = ~23 chars max
    // Each char in size 2 is ~12 pixels wide
    int genTextWidth = 13 * 12; // Just the label width for consistent alignment
    int cellTextWidth = 12 * 12; // "Max cells: " width
    
    // Center the text (screen width is 320)
    int genX = (SCREEN_WIDTH - genTextWidth) / 2 - 20;
    int cellX = (SCREEN_WIDTH - cellTextWidth) / 2 - 20;
    
    display.setCursor(genX, 130);
    display.print("Generations: ");
    display.println(generation);
    
    display.setCursor(cellX, 170);
    display.print("Max cells: ");
    display.println(max_cellCount);
}

void startShowingStats() {
    showingStats = true;
    statsStartTime = millis();
    showGameStatistics();
}

void playTone(int frequency, int duration) {
    if (!soundEnabled) return;
    
    // Adjust duration based on volume for noticeable difference
    int adjustedDuration;
    switch(soundVolume) {
        case 1: adjustedDuration = duration / 2; break;  // Low - half duration
        case 2: adjustedDuration = duration;     break;  // Medium - normal
        case 3: adjustedDuration = duration * 2; break;  // High - double duration
        default: adjustedDuration = duration;    break;
    }
    
    tone(BUZZER_PIN, frequency, adjustedDuration);
    delay(adjustedDuration);
    noTone(BUZZER_PIN);
}

void playMenuBeep() {
    if (!soundEnabled) return;
    playTone(1000, 50);  // 1kHz, 50ms
}

void playGameStart() {
    if (!soundEnabled) return;
    playTone(800, 100);
    delay(20);
    playTone(1200, 100);
    delay(20);
    playTone(1600, 100);
}

void playGameOver() {
    if (!soundEnabled) return;
    playTone(1600, 100);
    delay(20);
    playTone(1200, 100);
    delay(20);
    playTone(800, 150);
}

void playSpeedChange(int speedIndex) { 
    if (!soundEnabled) return;
    // Map speed index (0-11) to frequency range (500-2000 Hz)
    // Higher speed index = higher pitch
    int frequency = 500 + (speedIndex * 136);  // 136 = (2000-500)/11
    playTone(frequency, 80);
}

void playCellSizeChange() {
    if (!soundEnabled) return;
    // Quick ascending chirp to indicate cycling through sizes
    playTone(800, 40);
    delay(10);
    playTone(1200, 40);
    delay(10);
    playTone(1600, 40);
}

void playReset() {
    if (!soundEnabled) return;
    // Descending "swoosh" to indicate reset/regeneration
    playTone(2000, 60);
    delay(10);
    playTone(1500, 60);
    delay(10);
    playTone(1000, 60);
}

void playStartupTune() {
    // This function is no longer used - music is played inline during startup
    // Kept for compatibility but does nothing
}

void playColorToggle(bool colorModeOn) {
    if (!soundEnabled) return;
    
    if (colorModeOn) {
        // Color ON - bright ascending "rainbow" chirp
        playTone(800, 50);    // Start
        delay(5);
        playTone(1200, 50);   // Middle
        delay(5);
        playTone(1800, 80);   // High and hold slightly
    } else {
        // Color OFF - descending "back to mono"
        playTone(1800, 50);   // High
        delay(5);
        playTone(1200, 50);   // Middle
        delay(5);
        playTone(800, 80);    // Low and hold slightly
    }
}

void setup() {
    // pinMode(LED_BUILTIN, OUTPUT);  // WS2812 on GP16 - not standard LED
    
    pinMode(BTN_UP_PIN, INPUT_PULLUP);
    pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
    pinMode(BTN_LEFT_PIN, INPUT_PULLUP);
    pinMode(BTN_RIGHT_PIN, INPUT_PULLUP);
    pinMode(BTN_SET_PIN, INPUT_PULLUP);
    pinMode(BTN_B_PIN, INPUT_PULLUP);
    pinMode(BTN_RESET_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
digitalWrite(BUZZER_PIN, LOW);
    
    SPI1.setTX(TFT_MOSI);   // SDA on GP15
    SPI1.setSCK(TFT_SCK);   // SCL on GP14
    SPI1.begin();
    display.init(240, 320, SPI_MODE0);
    display.setRotation(3);
    display.fillScreen(COLOR_BLACK);
    
    updateBoardDimensions();

    // Load saved settings from EEPROM
    EEPROM.begin(256);
    soundEnabled = EEPROM.read(EEPROM_SOUND_ENABLED);
    soundVolume = EEPROM.read(EEPROM_SOUND_VOLUME);
    
    // Validate loaded values (in case EEPROM is uninitialized)
    if (soundVolume < 1 || soundVolume > 3) {
        soundVolume = 2;  // Default to Medium
        EEPROM.write(EEPROM_SOUND_VOLUME, soundVolume);
        EEPROM.commit();
    }

    // Person pattern animation with synchronized music
display.fillScreen(COLOR_BLACK);

// Calculate size to take 1/3 of screen - positioned much lower
int cellSize = 12;  // Large cells for visibility
int patternWidth = 7 * cellSize;
int patternHeight = 9 * cellSize;
int startX = (SCREEN_WIDTH - patternWidth) / 2;
int startY = (SCREEN_HEIGHT - patternHeight) / 2 + 60;  // Moved down by 60 pixels

// Draw person-shaped pattern (centered) in cyan
// Row 0: ..###..
display.fillRect(startX + 2*cellSize, startY, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 3*cellSize, startY, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 4*cellSize, startY, cellSize, cellSize, 0x07FF);

// Row 1: ..#.#..
display.fillRect(startX + 2*cellSize, startY + cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 4*cellSize, startY + cellSize, cellSize, cellSize, 0x07FF);

// Row 2: ..#.#..
display.fillRect(startX + 2*cellSize, startY + 2*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 4*cellSize, startY + 2*cellSize, cellSize, cellSize, 0x07FF);

// Row 3: ...#...
display.fillRect(startX + 3*cellSize, startY + 3*cellSize, cellSize, cellSize, 0x07FF);

// Row 4: #.###..
display.fillRect(startX, startY + 4*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 2*cellSize, startY + 4*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 3*cellSize, startY + 4*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 4*cellSize, startY + 4*cellSize, cellSize, cellSize, 0x07FF);

// Row 5: .#.#.#.
display.fillRect(startX + cellSize, startY + 5*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 3*cellSize, startY + 5*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 5*cellSize, startY + 5*cellSize, cellSize, cellSize, 0x07FF);

// Row 6: ...#..#
display.fillRect(startX + 3*cellSize, startY + 6*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 6*cellSize, startY + 6*cellSize, cellSize, cellSize, 0x07FF);

// Row 7: ..#.#..
display.fillRect(startX + 2*cellSize, startY + 7*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 4*cellSize, startY + 7*cellSize, cellSize, cellSize, 0x07FF);

// Row 8: ..#.#..
display.fillRect(startX + 2*cellSize, startY + 8*cellSize, cellSize, cellSize, 0x07FF);
display.fillRect(startX + 4*cellSize, startY + 8*cellSize, cellSize, cellSize, 0x07FF);

// START MUSIC immediately - Jurassic Park theme opening
if (soundEnabled) {
    // "Welcome... to Jurassic Park" - iconic 4-note opening
    tone(BUZZER_PIN, 659, 500);   // E5 - slower, more majestic
    delay(520);
    tone(BUZZER_PIN, 740, 450);   // F#5
    delay(470);
    tone(BUZZER_PIN, 831, 400);   // G#5
    delay(420);
    tone(BUZZER_PIN, 880, 900);   // A5 - hold the climax note longer
    delay(920);
    noTone(BUZZER_PIN);
} else {
    delay(2330);  // Same total time if sound is off
}

// CLEAR THE SCREEN before starting animation
display.fillScreen(COLOR_BLACK);

// Animate evolution
bool animBoard[40][30];  // Larger board so glider can fully exit
bool animNewBoard[40][30];
uint8_t animAges[40][30];  // Track cell ages for color

// Clear animation board
for (int x = 0; x < 40; x++) {
    for (int y = 0; y < 30; y++) {
        animBoard[x][y] = false;
        animNewBoard[x][y] = false;
        animAges[x][y] = 0;
    }
}

// Set person pattern in center of animation board
int centerX = 13;
int centerY = 15;

// Row 0: ..###..
animBoard[centerX + 2][centerY] = true;
animBoard[centerX + 3][centerY] = true;
animBoard[centerX + 4][centerY] = true;
animAges[centerX + 2][centerY] = 1;
animAges[centerX + 3][centerY] = 1;
animAges[centerX + 4][centerY] = 1;

// Row 1: ..#.#..
animBoard[centerX + 2][centerY + 1] = true;
animBoard[centerX + 4][centerY + 1] = true;
animAges[centerX + 2][centerY + 1] = 1;
animAges[centerX + 4][centerY + 1] = 1;

// Row 2: ..#.#..
animBoard[centerX + 2][centerY + 2] = true;
animBoard[centerX + 4][centerY + 2] = true;
animAges[centerX + 2][centerY + 2] = 1;
animAges[centerX + 4][centerY + 2] = 1;

// Row 3: ...#...
animBoard[centerX + 3][centerY + 3] = true;
animAges[centerX + 3][centerY + 3] = 1;

// Row 4: #.###..
animBoard[centerX][centerY + 4] = true;
animBoard[centerX + 2][centerY + 4] = true;
animBoard[centerX + 3][centerY + 4] = true;
animBoard[centerX + 4][centerY + 4] = true;
animAges[centerX][centerY + 4] = 1;
animAges[centerX + 2][centerY + 4] = 1;
animAges[centerX + 3][centerY + 4] = 1;
animAges[centerX + 4][centerY + 4] = 1;

// Row 5: .#.#.#.
animBoard[centerX + 1][centerY + 5] = true;
animBoard[centerX + 3][centerY + 5] = true;
animBoard[centerX + 5][centerY + 5] = true;
animAges[centerX + 1][centerY + 5] = 1;
animAges[centerX + 3][centerY + 5] = 1;
animAges[centerX + 5][centerY + 5] = 1;

// Row 6: ...#..#
animBoard[centerX + 3][centerY + 6] = true;
animBoard[centerX + 6][centerY + 6] = true;
animAges[centerX + 3][centerY + 6] = 1;
animAges[centerX + 6][centerY + 6] = 1;

// Row 7: ..#.#..
animBoard[centerX + 2][centerY + 7] = true;
animBoard[centerX + 4][centerY + 7] = true;
animAges[centerX + 2][centerY + 7] = 1;
animAges[centerX + 4][centerY + 7] = 1;

// Row 8: ..#.#..
animBoard[centerX + 2][centerY + 8] = true;
animBoard[centerX + 4][centerY + 8] = true;
animAges[centerX + 2][centerY + 8] = 1;
animAges[centerX + 4][centerY + 8] = 1;

// Run animation until all cells leave the screen or max generations
int maxGenerations = 200;  // Increased to allow glider to fully exit
int musicNote = 0;  // Track which note to play
unsigned long lastMusicTime = millis();

for (int gen = 0; gen < maxGenerations; gen++) {
    // Update game state - NO WRAPAROUND, cells at edges have fewer neighbors
    for (int x = 0; x < 40; x++) {
        for (int y = 0; y < 30; y++) {
            int neighbors = 0;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    // NO WRAPAROUND - cells outside bounds are considered dead
                    if (nx >= 0 && nx < 40 && ny >= 0 && ny < 30 && animBoard[nx][ny]) {
                        neighbors++;
                    }
                }
            }
            if (animBoard[x][y]) {
                animNewBoard[x][y] = (neighbors == 2 || neighbors == 3);
            } else {
                animNewBoard[x][y] = (neighbors == 3);
            }
        }
    }
    
    // Count cells still on screen and check if glider is near edge
    int cellsOnScreen = 0;
    int minX = 40, maxX = 0, minY = 30, maxY = 0;
    
    // Play continuing Jurassic Park melody during animation
    if (soundEnabled && gen % 8 == 0 && musicNote < 9) {
        switch(musicNote) {
            case 0: tone(BUZZER_PIN, 880, 400); break;   // A5 - echo of opening
            case 1: tone(BUZZER_PIN, 988, 700); break;   // B5 - sweeping up
            case 2: tone(BUZZER_PIN, 1047, 350); break;  // C6
            case 3: tone(BUZZER_PIN, 880, 350); break;   // A5 - back down
            case 4: tone(BUZZER_PIN, 988, 350); break;   // B5
            case 5: tone(BUZZER_PIN, 1047, 350); break;  // C6
            case 6: tone(BUZZER_PIN, 1175, 450); break;  // D6 - building
            case 7: tone(BUZZER_PIN, 1319, 500); break;  // E6 - climax
            case 8: tone(BUZZER_PIN, 1397, 900); break;  // F6 - triumphant finale!
        }
        musicNote++;
    }
    
    // Update ages and draw changed cells with color
    for (int x = 0; x < 40; x++) {
        for (int y = 0; y < 30; y++) {
            if (animBoard[x][y] != animNewBoard[x][y]) {
                animBoard[x][y] = animNewBoard[x][y];
                
                if (animNewBoard[x][y]) {
                    // Cell born - start at age 1
                    animAges[x][y] = 1;
                    display.fillRect(x * cellSize, y * cellSize, cellSize, cellSize, 
                                   getColorByAge(1));
                } else {
                    // Cell died
                    animAges[x][y] = 0;
                    display.fillRect(x * cellSize, y * cellSize, cellSize, cellSize, 
                                   COLOR_BLACK);
                }
            } else if (animBoard[x][y]) {
                // Cell stayed alive - increment age and update color
                if (animAges[x][y] < 255) {
                    animAges[x][y]++;
                }
                display.fillRect(x * cellSize, y * cellSize, cellSize, cellSize, 
                               getColorByAge(animAges[x][y]));
            }
            
            // Count cells and track bounds
            if (animBoard[x][y]) {
                cellsOnScreen++;
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    
    // Exit if all cells are gone OR if glider has completely left visible screen
    bool allCellsOffScreen = true;
    if (cellsOnScreen > 0) {
        if (minX < 27 && maxX >= 0 && minY < 20 && maxY >= 0) {
            allCellsOffScreen = false;
        }
    }
    
    if (cellsOnScreen == 0 || (gen > 15 && allCellsOffScreen)) {
        break;
    }
    
    delay(60);  // Faster animation
}

// NOW show title sequence after animation
    display.fillScreen(COLOR_BLACK);

    // Helper function to draw thick text by drawing multiple times with offset
    auto drawThickText = [&](const char* text, int x, int y, int textSize, uint16_t color) {
        display.setTextColor(color);
        // Draw text with thickness by drawing it multiple times with slight offsets
        for (int dx = 0; dx <= 1; dx++) {
            for (int dy = 0; dy <= 1; dy++) {
                display.setCursor(x + dx, y + dy);
                display.print(text);
            }
        }
    }; 

    // Helper function for flowing wave color animation (no screen clearing)
    auto drawWordWithWaveColors = [&](const char* word, int startY, int textSize, int duration) {
        display.setTextSize(textSize);
        int wordWidth = strlen(word) * textSize * 6;
        int startX = (SCREEN_WIDTH - wordWidth) / 2;  // Center horizontally
        
        unsigned long startTime = millis();
        while (millis() - startTime < duration) {
            unsigned long elapsed = millis() - startTime;
            int wavePhase = (elapsed / 3) % 360;  // Slower wave movement
            
            // Draw each character with wave-based color (overwrite in place)
            for (int i = 0; i < strlen(word); i++) {
                int charX = startX + i * textSize * 6;
                
                // Wave flows from left to right through the word - smoother gradient
                int colorPhase = (wavePhase + (i * 20)) % 360;
                
                uint16_t color;
                if (colorPhase < 60) {
                    // Red to Yellow
                    int r = 31;
                    int g = (colorPhase * 63) / 60;
                    int b = 0;
                    color = (r << 11) | (g << 5) | b;
                } else if (colorPhase < 120) {
                    // Yellow to Green
                    int r = 31 - ((colorPhase - 60) * 31) / 60;
                    int g = 63;
                    int b = 0;
                    color = (r << 11) | (g << 5) | b;
                } else if (colorPhase < 180) {
                    // Green to Cyan
                    int r = 0;
                    int g = 63;
                    int b = ((colorPhase - 120) * 31) / 60;
                    color = (r << 11) | (g << 5) | b;
                } else if (colorPhase < 240) {
                    // Cyan to Blue
                    int r = 0;
                    int g = 63 - ((colorPhase - 180) * 63) / 60;
                    int b = 31;
                    color = (r << 11) | (g << 5) | b;
                } else if (colorPhase < 300) {
                    // Blue to Magenta
                    int r = ((colorPhase - 240) * 31) / 60;
                    int g = 0;
                    int b = 31;
                    color = (r << 11) | (g << 5) | b;
                } else {
                    // Magenta to Red
                    int r = 31;
                    int g = 0;
                    int b = 31 - ((colorPhase - 300) * 31) / 60;
                    color = (r << 11) | (g << 5) | b;
                }
                
                char singleChar[2] = {word[i], '\0'};
                drawThickText(singleChar, charX, startY, textSize, color);
            }
            
            delay(20);
        }
    };

    // "GAME" - size 8, centered
drawWordWithWaveColors("GAME", 30, 8, 1200);   
delay(200);                                     

// "OF" - size 8, centered (keep GAME visible)
drawWordWithWaveColors("OF", 95, 8, 1200);     
delay(200);                                     

// "LIFE" - size 8, centered (keep GAME and OF visible)
drawWordWithWaveColors("LIFE", 160, 8, 1200);  

    clearBoard();
    showMenu();
    delay(50);
    
    collectRandomness(100);
}

void drawRuleIndicator() {
    if (!ruleExplorerMode) return;
    
    display.setTextSize(1);
    display.setTextColor(COLOR_WHITE);
    display.fillRect(5, 5, 90, 15, COLOR_BLACK);
    display.setCursor(8, 8);
    
    // Show B numbers
    display.print("B");
    for (int i = 0; i <= 8; i++) {
        if (birthRule & (1 << i)) {
            display.print(i);
        }
    }
    
    display.print("/S");
    for (int i = 0; i <= 8; i++) {
        if (survivalRule & (1 << i)) {
            display.print(i);
        }
    }
}

void drawPopCounter() {
    if (!showPopCounter || inMenuMode || editMode) return;
    
    // Draw semi-transparent background
    display.fillRect(5, 5, 110, 30, 0x2104);
    display.drawRect(5, 5, 100, 30, COLOR_WHITE);
    
    display.setTextSize(1);
    display.setTextColor(COLOR_WHITE);
    display.setCursor(8, 8);
    display.print("Gen:");
    display.println(generation);
    display.setCursor(8, 20);
    display.print("Pop:");
    display.println(cellCount);
}

void loop() {
    // Check for bootloader button press on GP1
    static unsigned long bootloaderButtonPressStart = 0;
    static bool bootloaderButtonPressed = false;
    
    if (isBtnReset() && !bootloaderButtonPressed) {
        bootloaderButtonPressed = true;
        bootloaderButtonPressStart = millis();
    }
    
    if (isBtnReset() && bootloaderButtonPressed && (millis() - bootloaderButtonPressStart >= 2000)) {
        // Long press (2 seconds) = enter bootloader mode
        display.fillScreen(COLOR_BLACK);
        display.setTextSize(2);
        display.setTextColor(COLOR_WHITE);
        display.setCursor(20, 100);
        display.println("BOOTLOADER MODE");
        display.setCursor(20, 130);
        display.println("Ready for upload");
        delay(500);
        rp2040.rebootToBootloader();  // Enter USB bootloader mode
    }
    
    if (!isBtnReset() && bootloaderButtonPressed) {
        bootloaderButtonPressed = false;
    }
    
    if (briansBrainMode && editMode && !briansBrainWasCustom) {
        editMode = false;
    }
    
    if (inRuleExplorerMenu) {
        handleRuleExplorerButtons();
        return;
    }
    
    handleDirectionalButtons();
    handleSetButton();
    handleButtonB();
    
    if (inMenuMode) {
        return;
    }
    
    if (!editMode) {
        unsigned long current_time = millis();
        
        // Don't update game if showing rules
        if (showingRules) {
            return;
        }
        
        // Clear color mode indicator after 2 seconds
        if (showingColorIndicator && (current_time - colorIndicatorTime >= 2000)) {
            showingColorIndicator = false;
            // Clear the indicator area by redrawing affected cells
            for (int x = (SCREEN_WIDTH - 60) / currentCellSize; x < SCREEN_WIDTH / currentCellSize; x++) {
                for (int y = 5 / currentCellSize; y < 25 / currentCellSize; y++) {
                    if (x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT) {
                        if (board[x][y]) {
                            if (colorMode && gameMode >= 1 && gameMode <= 4) {
                                drawCell(x, y, getColorByAge(cellAges[x][y]));
                            } else {
                                drawCell(x, y, COLOR_WHITE);
                            }
                        } else {
                            drawCell(x, y, COLOR_BLACK);
                        }
                    }
                }
            }
        }
        
        // Clear cell size indicator after 2 seconds
        if (showingCellSizeIndicator && (current_time - cellSizeIndicatorTime >= 2000)) {
            showingCellSizeIndicator = false;
            // Clear the indicator area by redrawing affected cells
            for (int x = (SCREEN_WIDTH - 60) / currentCellSize; x < SCREEN_WIDTH / currentCellSize; x++) {
                for (int y = 5 / currentCellSize; y < 25 / currentCellSize; y++) {
                    if (x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT) {
                        if (board[x][y]) {
                            if (colorMode && gameMode >= 1 && gameMode <= 4) {
                                drawCell(x, y, getColorByAge(cellAges[x][y]));
                            } else {
                                drawCell(x, y, COLOR_WHITE);
                            }
                        } else {
                            drawCell(x, y, COLOR_BLACK);
                        }
                    }
                }
            }
        }
        
        if (showingStats) {

          // Draw rule indicator if in Rule Explorer mode
        if (ruleExplorerMode && !showingCellSizeIndicator && !showingColorIndicator) {
            drawRuleIndicator();
        }

    if (current_time - statsStartTime >= 4000) {
        showingStats = false;
                display.fillScreen(COLOR_BLACK);
                if (showGridLines) {
                    drawFullGrid();
                }
                
                if (briansBrainMode) {
                    clearBoard();  // Always clear first
                    
                    if (briansBrainWasCustom) {
                        editMode = true;
                        cursorX = BOARD_WIDTH / 2;
                        cursorY = BOARD_HEIGHT / 2;
                    } else {
                        switch(currentBriansBrainSize) {
                            case 1: generateBriansBrainSmall(); break;
                            case 2: generateBriansBrainMedium(); break;
                            case 3: generateBriansBrainLarge(); break;
                            case 4: generateBriansBrainRandom(); break;
                        }
                        generation = 0;
                        cellCount = 0;
                        frame_start_t = millis();
                    }
                    start_t = micros();
                } else if (dayNightMode) {
                    dayNightMode = false;
                    editMode = false;
                    inMenuMode = true;
                    inDayNightSubMenu = true;
                    inAltGamesSubMenu = false;
                    dayNightDensitySelection = 1;
                    showMenu();
                } else if (seedsMode) {
                    seedsMode = false;
                    editMode = false;
                    inMenuMode = true;
                    inSeedsSubMenu = true;
                    inAltGamesSubMenu = false;
                    seedsGameSelection = 1;
                    showMenu();
                } else if (cyclicMode) {
                    cyclicMode = false;
                    editMode = false;
                    inMenuMode = true;
                    inCyclicSubMenu = true;
                    inAltGamesSubMenu = false;
                    cyclicSizeSelection = 1;
                    showMenu();
                    } else {
                    // Go back to appropriate screen based on game mode
                    if (gameMode == 2) {
                        // Custom game - return to edit mode, not menu
                        clearBoard();
                        editMode = true;
                        inMenuMode = false;
                        cursorX = BOARD_WIDTH / 2;
                        cursorY = BOARD_HEIGHT / 2;
                        generation = 0;
                        cellCount = 0;
                        max_cellCount = 0;
                        currentSpeedIndex = 0;  // Reset to slowest speed for next game
                    } else {
                        inMenuMode = true;
                        menuSelection = 1;
                        showMenu();
                    }
                }
                
                generation = 0;
                start_t = micros();
            }
            return;
        }
        
        if (current_time - frame_start_t >= frameDelays[currentSpeedIndex]) {
            frame_start_t = current_time;
            generation++;
            
            switch (gameMode) {
                case 5:
                    if (dayNightMode) {
                        updateDayNight();
                        drawDayNight();
                        drawPopCounter();
                        return;
                    }
                    break;
                    
                case 6:
                    if (seedsMode) {
                        if (editMode) {
                            display.fillScreen(COLOR_BLACK);
                            for (int x = 0; x < BOARD_WIDTH; x++) {
                                for (int y = 0; y < BOARD_HEIGHT; y++) {
                                    if (board[x][y]) {
                                        drawCell(x, y, COLOR_WHITE);
                                    }
                                }
                            }
                            drawCursor();
                            return;
                        } else {
                            updateSeeds();
                            drawSeeds();
                            drawPopCounter();
                            
                            if (cellCount == 0 || (cellCount > 500 && generation > 150)) {
                                if (seedsWasCustom) {
                                    clearBoard();
                                    editMode = true;
                                    cursorX = BOARD_WIDTH / 2;
                                    cursorY = BOARD_HEIGHT / 2;
                                } else {
                                    initializeSeeds();
                                }
                                generation = 0;
                            }
                            return;
                        }
                    }
                    break;
                    
                case 8:
                    if (briansBrainMode) {
                        updateBriansBrain();
                        drawBriansBrain();
                        drawPopCounter();
                        
                        if (cellCount < 10 && generation > 20) {
                            briansBrain_lowCellCounter++;
                            if (briansBrain_lowCellCounter >= 15) {
                                startShowingStats();
                                briansBrain_lowCellCounter = 0;
                                return;
                            }
                        } else {
                            briansBrain_lowCellCounter = 0;
                        }
                        
                        if (cellCount == 0 && generation > 10) {
                            startShowingStats();
                            briansBrain_lowCellCounter = 0;
                            return;
                        }
                        return;
                    }
                    break;
                    
                case 9:
                    if (cyclicMode) {
                        updateCyclic();
                        drawCyclic();
                        drawPopCounter();
                        return;
                    }
                    break;
            }
            
         if (gameMode >= 1 && gameMode <= 4) {
    updateGame();
    drawPopCounter();
    
    // Better stagnation detection - track complete board state
    static uint32_t stableFrameCount = 0;
    static uint32_t lastFourHashes[4] = {0, 0, 0, 0};
    
    // Create a hash of the board state (includes position)
    uint32_t board_hash = 0;
    for (int x = 0; x < BOARD_WIDTH && x < 40; x++) {
        for (int y = 0; y < BOARD_HEIGHT && y < 30; y++) {
            if (board[x][y]) {
                board_hash += (x * 997 + y * 991);  // Use prime numbers for better distribution
            }
        }
    }
    
    // Check for period-1 (static) or period-2 (blinker) patterns
    bool isPeriod1 = (board_hash == lastFourHashes[0] && generation > 50);
    bool isPeriod2 = (board_hash == lastFourHashes[1] && 
                      lastFourHashes[0] == lastFourHashes[2] && 
                      generation > 50);
    
    if ((isPeriod1 || isPeriod2) && (gameMode == 1 || gameMode == 2) && !ruleExplorerMode) {
        stableFrameCount++;
        if (stableFrameCount > 15) {  // Stable for 15 frames
            startShowingStats();
            stableFrameCount = 0;
            // Reset hash history
            lastFourHashes[0] = 0;
            lastFourHashes[1] = 0;
            lastFourHashes[2] = 0;
            lastFourHashes[3] = 0;
            return;
        }
    } else {
        stableFrameCount = 0;
    }
    
    // Shift the hash history
    lastFourHashes[3] = lastFourHashes[2];
    lastFourHashes[2] = lastFourHashes[1];
    lastFourHashes[1] = lastFourHashes[0];
    lastFourHashes[0] = board_hash;
    
    // End if truly dead
    if (cellCount == 0 && generation > 20 && (gameMode == 1 || gameMode == 2) && !ruleExplorerMode) {
        startShowingStats();
        stableFrameCount = 0;
        lastFourHashes[0] = 0;
        lastFourHashes[1] = 0;
        lastFourHashes[2] = 0;
        lastFourHashes[3] = 0;
        return;
    }
}  
        }
    } else if (editMode && (gameMode == 2 || gameMode == 6 || (gameMode == 8 && briansBrainWasCustom))) {
        // Only redraw cursor area - don't clear entire screen every frame
        static int lastCursorX = -1;
        static int lastCursorY = -1;
        static bool needsFullRedraw = true;
        
        // Detect if we just entered edit mode (cursor position reset to center)
        if (lastCursorX == -1 || needsFullRedraw) {
            display.fillScreen(COLOR_BLACK);
            for (int x = 0; x < BOARD_WIDTH; x++) {
                for (int y = 0; y < BOARD_HEIGHT; y++) {
                    if (board[x][y]) {
                        drawCell(x, y, COLOR_WHITE);
                    }
                }
            }
            needsFullRedraw = false;
            lastCursorX = cursorX;
            lastCursorY = cursorY;
        }
        
        // If cursor moved, redraw old position cells to their normal state
        if (lastCursorX != cursorX || lastCursorY != cursorY) {
            // Redraw old cursor position and crosshair
            drawCell(lastCursorX, lastCursorY, board[lastCursorX][lastCursorY] ? COLOR_WHITE : COLOR_BLACK);
            if (lastCursorX > 0) drawCell(lastCursorX-1, lastCursorY, board[lastCursorX-1][lastCursorY] ? COLOR_WHITE : COLOR_BLACK);
            if (lastCursorX < BOARD_WIDTH-1) drawCell(lastCursorX+1, lastCursorY, board[lastCursorX+1][lastCursorY] ? COLOR_WHITE : COLOR_BLACK);
            if (lastCursorY > 0) drawCell(lastCursorX, lastCursorY-1, board[lastCursorX][lastCursorY-1] ? COLOR_WHITE : COLOR_BLACK);
            if (lastCursorY < BOARD_HEIGHT-1) drawCell(lastCursorX, lastCursorY+1, board[lastCursorX][lastCursorY+1] ? COLOR_WHITE : COLOR_BLACK);
            
            lastCursorX = cursorX;
            lastCursorY = cursorY;
        }
        
        // Always draw cursor at current position (for blinking)
        drawCursor();
    } else if (!editMode && gameMode == 3) {
        display.fillScreen(COLOR_BLACK);
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                if (board[x][y]) {
                    drawCell(x, y, COLOR_WHITE);
                }
            }
        }
    }
}

void updateGame() {
    cellCount = 0;
    
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            int neighbors = 0;
            
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    int nx = (x + dx + BOARD_WIDTH) % BOARD_WIDTH;
                    int ny = (y + dy + BOARD_HEIGHT) % BOARD_HEIGHT;
                    
                    if (board[nx][ny]) {
                        neighbors++;
                    }
                }
            }
            
            // Apply rules - use Rule Explorer rules only in Rule Explorer mode
            if (ruleExplorerMode) {
                if (board[x][y]) {
                    // Check survival rule bit
                    newBoard[x][y] = (survivalRule & (1 << neighbors)) != 0;
                } else {
                    // Check birth rule bit
                    newBoard[x][y] = (birthRule & (1 << neighbors)) != 0;
                }
            } else {
                // Standard Conway's Game of Life rules (B3/S23)
                if (board[x][y]) {
                    newBoard[x][y] = (neighbors == 2 || neighbors == 3);
                } else {
                    newBoard[x][y] = (neighbors == 3);
                }
            }
            
            if (newBoard[x][y]) {
                cellCount++;
            }
        }
    }
    
    // Update board and ages
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            if (board[x][y] != newBoard[x][y]) {
                board[x][y] = newBoard[x][y];
                
                if (newBoard[x][y]) {
                    // Cell born - start at age 1
                    cellAges[x][y] = 1;
                } else {
                    // Cell died
                    if (trailMode) {
                        // In trail mode (both color and mono), set age to 1 to start fade
                        cellAges[x][y] = 1;
                    } else {
                        cellAges[x][y] = 0;
                    }
                }
                
                // Draw with appropriate color/trail
                if (trailMode && colorMode && !newBoard[x][y] && cellAges[x][y] > 0) {
                    // Trail mode + Color mode - show colored trail for dead cell
                    drawCell(x, y, getColorByAge(cellAges[x][y]));
                } else if (trailMode && !colorMode && cellAges[x][y] > 0 && !newBoard[x][y]) {
                    // Trail mode + Mono mode - fade dead cells through gray shades
                    uint16_t fadeColor;
                    if (cellAges[x][y] < 3) {
                        fadeColor = 0xCE59;  // Light gray
                    } else if (cellAges[x][y] < 6) {
                        fadeColor = 0x8410;  // Medium gray
                    } else if (cellAges[x][y] < 9) {
                        fadeColor = 0x4208;  // Dark gray
                    } else if (cellAges[x][y] < 12) {
                        fadeColor = 0x2104;  // Very dark gray
                    } else {
                        fadeColor = COLOR_BLACK;
                        cellAges[x][y] = 0;  // Stop tracking
                    }
                    drawCell(x, y, fadeColor);
                } else if (colorMode && gameMode >= 1 && gameMode <= 4) {
                    // Color mode without trail
                    drawCell(x, y, getColorByAge(cellAges[x][y]));
                } else {
                    // Mono mode without trail
                    drawCell(x, y, newBoard[x][y] ? COLOR_WHITE : COLOR_BLACK);
                }
            } else if (board[x][y] && colorMode && gameMode >= 1 && gameMode <= 4) {
                // Cell stayed alive - increment age (cap at 255)
                if (cellAges[x][y] < 255) {
                    cellAges[x][y]++;
                    // Redraw with updated color
                    drawCell(x, y, getColorByAge(cellAges[x][y]));
                }
            } else if (!board[x][y] && trailMode && cellAges[x][y] > 0) {
                // Continue aging trail for dead cells (both color and mono modes)
                cellAges[x][y]++;
                if (cellAges[x][y] >= 12) {
                    cellAges[x][y] = 0;
                    drawCell(x, y, COLOR_BLACK);
                } else {
                    // Redraw with fading trail
                    if (colorMode) {
                        // Color mode trail - use age-based colors
                        drawCell(x, y, getColorByAge(cellAges[x][y]));
                    } else {
                        // Mono mode trail - use gray shades
                        uint16_t fadeColor;
                        if (cellAges[x][y] < 3) {
                            fadeColor = 0xCE59;  // Light gray
                        } else if (cellAges[x][y] < 6) {
                            fadeColor = 0x8410;  // Medium gray
                        } else if (cellAges[x][y] < 9) {
                            fadeColor = 0x4208;  // Dark gray
                        } else {
                            fadeColor = 0x2104;  // Very dark gray
                        }
                        drawCell(x, y, fadeColor);
                    }
                }
            }
        }
    }
    
    if (cellCount > max_cellCount) {
        max_cellCount = cellCount;
    }
}

void drawBoard() {
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            drawCell(x, y, board[x][y] ? COLOR_WHITE : COLOR_BLACK);
        }
    }
}

void randomizeBoard() {
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            board[x][y] = (random(100) < 30);
            cellAges[x][y] = board[x][y] ? 1 : 0;
        }
    }
}

void drawCellWithGrid(int x, int y, uint16_t color) {
    // Don't draw cells in the population counter area (top-left 105x35 pixels)
    if (showPopCounter && !inMenuMode && !editMode) {
        int cellLeft = x * currentCellSize;
        int cellTop = y * currentCellSize;
        int cellRight = cellLeft + currentCellSize;
        int cellBottom = cellTop + currentCellSize;
        
        // Skip if cell overlaps with population counter area (5,5 to 105,35)
        if (cellLeft < 105 && cellRight > 5 && cellTop < 35 && cellBottom > 5) {
            return;  // Don't draw this cell
        }
    }
    
    if (showGridLines && currentCellSize >= 3) {
        // Draw cell with 1-pixel border, leaving space for grid
        if (color == COLOR_BLACK) {
            // For black cells, just draw the cell area (grid stays visible)
            display.fillRect(x * currentCellSize + 1, y * currentCellSize + 1, 
                            currentCellSize - 1, currentCellSize - 1, COLOR_BLACK);
        } else {
            // For colored cells, draw the cell
            display.fillRect(x * currentCellSize + 1, y * currentCellSize + 1, 
                            currentCellSize - 1, currentCellSize - 1, color);
        }
        // Always draw grid lines (even for black cells)
        display.drawFastHLine(x * currentCellSize, y * currentCellSize, currentCellSize, 0x2104);
        display.drawFastVLine(x * currentCellSize, y * currentCellSize, currentCellSize, 0x2104);
    } else {
        // Normal drawing without grid
        display.fillRect(x * currentCellSize, y * currentCellSize, currentCellSize, currentCellSize, color);
    }
}

void drawFullGrid() {
    if (!showGridLines || currentCellSize < 3) return;
    
    // Draw vertical lines
    for (int x = 0; x <= BOARD_WIDTH; x++) {
        display.drawFastVLine(x * currentCellSize, 0, SCREEN_HEIGHT, 0x2104);
    }
    
    // Draw horizontal lines
    for (int y = 0; y <= BOARD_HEIGHT; y++) {
        display.drawFastHLine(0, y * currentCellSize, SCREEN_WIDTH, 0x2104);
    }
}

void drawCell(int x, int y, uint16_t color) {
    drawCellWithGrid(x, y, color);
}

uint16_t getColorByAge(uint8_t age) {
  uint16_t getDayNightColorByAge(uint8_t age);
    if (age == 0) return COLOR_BLACK;
    
    // Color progression: Blue -> Cyan -> Green -> Yellow -> Red -> Magenta
    if (age < 5) {
        // Blue (0,0,31) to Cyan (0,63,31)
        int g = (age * 63) / 5;
        return (0 << 11) | (g << 5) | 31;
    } else if (age < 10) {
        // Cyan to Green (0,63,0)
        int b = 31 - ((age - 5) * 31) / 5;
        return (0 << 11) | (63 << 5) | b;
    } else if (age < 20) {
        // Green to Yellow (31,63,0)
        int r = ((age - 10) * 31) / 10;
        return (r << 11) | (63 << 5) | 0;
    } else if (age < 40) {
        // Yellow to Red (31,0,0)
        int g = 63 - ((age - 20) * 63) / 20;
        return (31 << 11) | (g << 5) | 0;
    } else if (age < 80) {
        // Red to Magenta (31,0,31)
        int b = ((age - 40) * 31) / 40;
        return (31 << 11) | (0 << 5) | b;
    } else {
        // Bright Magenta for very old cells
        return 0xF81F;  // Magenta
    }
}

uint16_t getDayNightColorByAge(uint8_t age, bool isAlive) {
    if (isAlive) {
        // Living cells: Warm day colors based on age
        if (age < 5) {
            // Bright white (noon sun)
            return 0xFFFF;
        } else if (age < 15) {
            // Bright yellow to gold (morning/afternoon)
            int r = 31;
            int g = 63 - ((age - 5) * 10) / 10;
            int b = 0;
            return (r << 11) | (g << 5) | b;
        } else if (age < 30) {
            // Gold to orange (late afternoon)
            int r = 31;
            int g = 53 - ((age - 15) * 20) / 15;
            int b = 0;
            return (r << 11) | (g << 5) | b;
        } else {
            // Deep orange/red (sunset)
            return 0xF800;
        }
    } else {
        // Dead cells with trail: Cool night colors
        if (age == 0) {
            return COLOR_BLACK;  // No trail
        } else if (age == 1) {
            // Twilight purple
            return 0x780F;
        } else if (age < 5) {
            // Deep blue (early night)
            int r = 0;
            int g = 0;
            int b = 31 - ((age - 1) * 8);
            return (r << 11) | (g << 5) | b;
        } else if (age < 10) {
            // Darker blue (midnight)
            int r = 0;
            int g = 0;
            int b = 15 - ((age - 5) * 3);
            return (r << 11) | (g << 5) | b;
        } else if (age < 15) {
            // Very dark blue (deep night)
            int r = 0;
            int g = 0;
            int b = 5 - ((age - 10) / 2);
            return (r << 11) | (g << 5) | b;
        } else {
            // Fade to black
            return COLOR_BLACK;
        }
    }
}

void collectRandomness(int ms) {
    uint32_t wait_t = millis();
    uint32_t r = 0;
    while (millis() - wait_t < ms) {
        r += digitalRead(BTN_B_PIN);
    }
    randomSeed(r);
}

void clearBoard() {
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            board[x][y] = false;
            newBoard[x][y] = false;
            briansBrain_dying[x][y] = false;
            briansBrain_newDying[x][y] = false;
            cyclicStates[x][y] = 0;
            cyclicNewStates[x][y] = 0;
            cellAges[x][y] = 0;
            seedsAges[x][y] = 0;
        }
    }
    cellCount = 0;
    max_cellCount = 0;
    generation = 0;
    display.fillScreen(COLOR_BLACK);
}

void flashLED() {
    // LED not available on Waveshare RP2040 Zero
    // digitalWrite(LED_BUILTIN, HIGH);
    // delay(5);
    // digitalWrite(LED_BUILTIN, LOW);
}

void loadPresetPattern(int patternId) {
    clearBoard();
    if (showGridLines) {
        drawFullGrid();
    }
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    
    switch (patternId) {
        case 1: // Coe Ship
            // Row y-4: cells at x+4,5,6,7,8,9
            board[centerX+4][centerY-4] = true;
            board[centerX+5][centerY-4] = true;
            board[centerX+6][centerY-4] = true;
            board[centerX+7][centerY-4] = true;
            board[centerX+8][centerY-4] = true;
            board[centerX+9][centerY-4] = true;
            
            // Row y-3: cells at x+2,3,9
            board[centerX+2][centerY-3] = true;
            board[centerX+3][centerY-3] = true;
            board[centerX+9][centerY-3] = true;
            
            // Row y-2: cells at x+0,1,3,9
            board[centerX][centerY-2] = true;
            board[centerX+1][centerY-2] = true;
            board[centerX+3][centerY-2] = true;
            board[centerX+9][centerY-2] = true;
            
            // Row y-1: cells at x+4,8
            board[centerX+4][centerY-1] = true;
            board[centerX+8][centerY-1] = true;
            
            // Row y: cell at x+6
            board[centerX+6][centerY] = true;
            
            // Row y+1: cells at x+6,7
            board[centerX+6][centerY+1] = true;
            board[centerX+7][centerY+1] = true;
            
            // Row y+2: cells at x+5,6,7,8
            board[centerX+5][centerY+2] = true;
            board[centerX+6][centerY+2] = true;
            board[centerX+7][centerY+2] = true;
            board[centerX+8][centerY+2] = true;
            
            // Row y+3: cells at x+5,6,8,9
            board[centerX+5][centerY+3] = true;
            board[centerX+6][centerY+3] = true;
            board[centerX+8][centerY+3] = true;
            board[centerX+9][centerY+3] = true;
            
            // Row y+4: cells at x+7,8
            board[centerX+7][centerY+4] = true;
            board[centerX+8][centerY+4] = true;
            break;
            
        case 2: // Gosper Glider Gun
            // Left square
            board[centerX-18][centerY] = true;
            board[centerX-18][centerY+1] = true;
            board[centerX-17][centerY] = true;
            board[centerX-17][centerY+1] = true;
            
            // Left pattern
            board[centerX-8][centerY] = true;
            board[centerX-8][centerY-1] = true;
            board[centerX-8][centerY+1] = true;
            board[centerX-7][centerY-2] = true;
            board[centerX-7][centerY+2] = true;
            board[centerX-6][centerY-3] = true;
            board[centerX-6][centerY+3] = true;
            board[centerX-5][centerY-3] = true;
            board[centerX-5][centerY+3] = true;
            board[centerX-4][centerY] = true;
            board[centerX-3][centerY-2] = true;
            board[centerX-3][centerY+2] = true;
            board[centerX-2][centerY-1] = true;
            board[centerX-2][centerY] = true;
            board[centerX-2][centerY+1] = true;
            board[centerX-1][centerY] = true;
            
            // Right pattern
            board[centerX+2][centerY-1] = true;
            board[centerX+2][centerY-2] = true;
            board[centerX+2][centerY-3] = true;
            board[centerX+3][centerY-1] = true;
            board[centerX+3][centerY-2] = true;
            board[centerX+3][centerY-3] = true;
            board[centerX+4][centerY] = true;
            board[centerX+4][centerY-4] = true;
            board[centerX+6][centerY-5] = true;
            board[centerX+6][centerY-4] = true;
            board[centerX+6][centerY] = true;
            board[centerX+6][centerY+1] = true;
            
            // Right square
            board[centerX+16][centerY-2] = true;
            board[centerX+16][centerY-3] = true;
            board[centerX+17][centerY-2] = true;
            board[centerX+17][centerY-3] = true;
            break;
            
        case 3: // 4-8-12 Diamond
            // Row y-4: 4 cells
            board[centerX-2][centerY-4] = true;
            board[centerX-1][centerY-4] = true;
            board[centerX][centerY-4] = true;
            board[centerX+1][centerY-4] = true;
            
            // Row y-2: 8 cells
            board[centerX-4][centerY-2] = true;
            board[centerX-3][centerY-2] = true;
            board[centerX-2][centerY-2] = true;
            board[centerX-1][centerY-2] = true;
            board[centerX][centerY-2] = true;
            board[centerX+1][centerY-2] = true;
            board[centerX+2][centerY-2] = true;
            board[centerX+3][centerY-2] = true;
            
            // Row y: 12 cells
            board[centerX-6][centerY] = true;
            board[centerX-5][centerY] = true;
            board[centerX-4][centerY] = true;
            board[centerX-3][centerY] = true;
            board[centerX-2][centerY] = true;
            board[centerX-1][centerY] = true;
            board[centerX][centerY] = true;
            board[centerX+1][centerY] = true;
            board[centerX+2][centerY] = true;
            board[centerX+3][centerY] = true;
            board[centerX+4][centerY] = true;
            board[centerX+5][centerY] = true;
            
            // Row y+2: 8 cells
            board[centerX-4][centerY+2] = true;
            board[centerX-3][centerY+2] = true;
            board[centerX-2][centerY+2] = true;
            board[centerX-1][centerY+2] = true;
            board[centerX][centerY+2] = true;
            board[centerX+1][centerY+2] = true;
            board[centerX+2][centerY+2] = true;
            board[centerX+3][centerY+2] = true;
            
            // Row y+4: 4 cells
            board[centerX-2][centerY+4] = true;
            board[centerX-1][centerY+4] = true;
            board[centerX][centerY+4] = true;
            board[centerX+1][centerY+4] = true;
            break;
            
        case 4: // Achim's p144
            // Row y-9
            board[centerX-14][centerY-9] = true;
            board[centerX-13][centerY-9] = true;
            board[centerX+12][centerY-9] = true;
            board[centerX+13][centerY-9] = true;
            
            // Row y-8
            board[centerX-14][centerY-8] = true;
            board[centerX-13][centerY-8] = true;
            board[centerX+12][centerY-8] = true;
            board[centerX+13][centerY-8] = true;
            
            // Row y-7
            board[centerX+4][centerY-7] = true;
            board[centerX+5][centerY-7] = true;
            
            // Row y-6
            board[centerX+3][centerY-6] = true;
            board[centerX+6][centerY-6] = true;
            
            // Row y-5
            board[centerX+4][centerY-5] = true;
            board[centerX+5][centerY-5] = true;
            
            // Row y-4
            board[centerX][centerY-4] = true;
            
            // Row y-3
            board[centerX-1][centerY-3] = true;
            board[centerX+1][centerY-3] = true;
            
            // Row y-2
            board[centerX-2][centerY-2] = true;
            board[centerX+2][centerY-2] = true;
            
            // Row y-1
            board[centerX-2][centerY-1] = true;
            board[centerX+1][centerY-1] = true;
            
            // Row y+1
            board[centerX-2][centerY+1] = true;
            board[centerX+1][centerY+1] = true;
            
            // Row y+2
            board[centerX-3][centerY+2] = true;
            board[centerX+1][centerY+2] = true;
            
            // Row y+3
            board[centerX-2][centerY+3] = true;
            board[centerX][centerY+3] = true;
            
            // Row y+4
            board[centerX-1][centerY+4] = true;
            
            // Row y+5
            board[centerX-6][centerY+5] = true;
            board[centerX-5][centerY+5] = true;
            
            // Row y+6
            board[centerX-7][centerY+6] = true;
            board[centerX-4][centerY+6] = true;
            
            // Row y+7
            board[centerX-6][centerY+7] = true;
            board[centerX-5][centerY+7] = true;
            
            // Row y+8
            board[centerX-14][centerY+8] = true;
            board[centerX-13][centerY+8] = true;
            board[centerX+12][centerY+8] = true;
            board[centerX+13][centerY+8] = true;
            
            // Row y+9
            board[centerX-14][centerY+9] = true;
            board[centerX-13][centerY+9] = true;
            board[centerX+12][centerY+9] = true;
            board[centerX+13][centerY+9] = true;
            break;
            
        case 5: // 56P6H1V0
            // Row y-6
            board[centerX-8][centerY-6] = true;
            board[centerX-7][centerY-6] = true;
            board[centerX-6][centerY-6] = true;
            board[centerX+5][centerY-6] = true;
            board[centerX+6][centerY-6] = true;
            board[centerX+7][centerY-6] = true;
            
            // Row y-5
            board[centerX-13][centerY-5] = true;
            board[centerX-12][centerY-5] = true;
            board[centerX-11][centerY-5] = true;
            board[centerX-9][centerY-5] = true;
            board[centerX-1][centerY-5] = true;
            board[centerX][centerY-5] = true;
            board[centerX+8][centerY-5] = true;
            board[centerX+10][centerY-5] = true;
            board[centerX+11][centerY-5] = true;
            board[centerX+12][centerY-5] = true;
            
            // Row y-4
            board[centerX-9][centerY-4] = true;
            board[centerX-5][centerY-4] = true;
            board[centerX-2][centerY-4] = true;
            board[centerX+1][centerY-4] = true;
            board[centerX+4][centerY-4] = true;
            board[centerX+8][centerY-4] = true;
            
            // Row y-3
            board[centerX-9][centerY-3] = true;
            board[centerX-3][centerY-3] = true;
            board[centerX+2][centerY-3] = true;
            board[centerX+8][centerY-3] = true;
            
            // Row y-2
            board[centerX-3][centerY-2] = true;
            board[centerX-2][centerY-2] = true;
            board[centerX+1][centerY-2] = true;
            board[centerX+2][centerY-2] = true;
            
            // Row y-1
            board[centerX-6][centerY-1] = true;
            board[centerX-2][centerY-1] = true;
            board[centerX+1][centerY-1] = true;
            board[centerX+5][centerY-1] = true;
            
            // Row y
            board[centerX-6][centerY] = true;
            board[centerX-4][centerY] = true;
            board[centerX+3][centerY] = true;
            board[centerX+5][centerY] = true;
            
            // Row y+1
            board[centerX-5][centerY+1] = true;
            board[centerX-4][centerY+1] = true;
            board[centerX-3][centerY+1] = true;
            board[centerX-2][centerY+1] = true;
            board[centerX-1][centerY+1] = true;
            board[centerX][centerY+1] = true;
            board[centerX+1][centerY+1] = true;
            board[centerX+2][centerY+1] = true;
            board[centerX+3][centerY+1] = true;
            board[centerX+4][centerY+1] = true;
            
            // Row y+2
            board[centerX-3][centerY+2] = true;
            board[centerX+2][centerY+2] = true;
            
            // Row y+3
            board[centerX-5][centerY+3] = true;
            board[centerX+4][centerY+3] = true;
            
            // Row y+4
            board[centerX-6][centerY+4] = true;
            board[centerX+5][centerY+4] = true;
            
            // Row y+5
            board[centerX-5][centerY+5] = true;
            board[centerX+4][centerY+5] = true;
            break;
    }
    
    cellCount = 0;
    max_cellCount = 0;
    generation = 0;
}

void generateSymmetricPattern(int symmetryType) {
    clearBoard();
    switch(symmetryType) {
        case 1: generateVerticalSymmetric(); break;
        case 2: generateHorizontalSymmetric(); break;
        case 3: generateRotationalSymmetric(); break;
    }
}

void generateVerticalSymmetric() {
    int halfWidth = BOARD_WIDTH / 2;
    for (int x = 0; x < halfWidth; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            if (random(100) < 20) {
                board[x][y] = true;
                board[BOARD_WIDTH - 1 - x][y] = true;
            }
        }
    }
}

void generateHorizontalSymmetric() {
    int halfHeight = BOARD_HEIGHT / 2;
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < halfHeight; y++) {
            if (random(100) < 20) {
                board[x][y] = true;
                board[x][BOARD_HEIGHT - 1 - y] = true;
            }
        }
    }
}

void generateRotationalSymmetric() {
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    for (int x = 0; x < centerX; x++) {
        for (int y = 0; y < centerY; y++) {
            if (random(100) < 16) {
                board[x][y] = true;
                board[BOARD_WIDTH - 1 - x][BOARD_HEIGHT - 1 - y] = true;
                board[BOARD_WIDTH - 1 - x][y] = true;
                board[x][BOARD_HEIGHT - 1 - y] = true;
            }
        }
    }
}

void generateSmallSymmetricPattern(int symmetryType) {
    clearBoard();
    int scale = (currentCellSize == 2) ? 2 : 1;
    
    switch(symmetryType) {
        case 1: {
            int halfWidth = BOARD_WIDTH / 2;
            int startX = halfWidth - (8 * scale);
            int endX = halfWidth - 1;
            int startY = 12 * scale;
            int endY = 20 * scale;
            if (endY > BOARD_HEIGHT) endY = BOARD_HEIGHT;
            
            for (int x = startX; x < endX; x++) {
                for (int y = startY; y < endY; y++) {
                    if (random(100) < 33) {
                        board[x][y] = true;
                        board[BOARD_WIDTH - 1 - x][y] = true;
                    }
                }
            }
            break;
        }
        case 2: {
            int startX = BOARD_WIDTH/2 - (12 * scale);
            int endX = BOARD_WIDTH/2 + (12 * scale);
            startX = (startX < 0) ? 0 : startX;
            endX = (endX >= BOARD_WIDTH) ? BOARD_WIDTH : endX;
            int halfHeight = BOARD_HEIGHT / 2;
            int startY = 4 * scale;
            int endY = halfHeight - 2;
            
            for (int x = startX; x < endX; x++) {
                for (int y = startY; y < endY; y++) {
                    if (random(100) < 25) {
                        board[x][y] = true;
                        board[x][BOARD_HEIGHT - 1 - y] = true;
                    }
                }
            }
            break;
        }
        case 3: {
            int centerX = BOARD_WIDTH / 2;
            int centerY = BOARD_HEIGHT / 2;
            int rangeX = 8 * scale;
            int rangeY = 6 * scale;
            
            for (int x = centerX - rangeX; x < centerX; x++) {
                for (int y = centerY - rangeY; y < centerY; y++) {
                    if (random(100) < 20) {
                        board[x][y] = true;
                        board[BOARD_WIDTH - 1 - x][BOARD_HEIGHT - 1 - y] = true;
                        board[BOARD_WIDTH - 1 - x][y] = true;
                        board[x][BOARD_HEIGHT - 1 - y] = true;
                    }
                }
            }
            break;
        }
    }
}

void generateMediumSymmetricPattern(int symmetryType) {
    clearBoard();
    int scale = (currentCellSize == 2) ? 2 : 1;
    
    switch(symmetryType) {
        case 1: {
            int halfWidth = BOARD_WIDTH / 2;
            int startX = halfWidth - (18 * scale);
            int endX = halfWidth - 2;
            int startY = 2 * scale;
            int endY = 30 * scale;
            if (endY > BOARD_HEIGHT) endY = BOARD_HEIGHT;
            
            for (int x = startX; x < endX; x++) {
                for (int y = startY; y < endY; y++) {
                    if (random(100) < 20) {
                        board[x][y] = true;
                        board[BOARD_WIDTH - 1 - x][y] = true;
                    }
                }
            }
            break;
        }
        case 2: {
            int startX = BOARD_WIDTH/2 - (25 * scale);
            int endX = BOARD_WIDTH/2 + (25 * scale);
            startX = (startX < 0) ? 0 : startX;
            endX = (endX >= BOARD_WIDTH) ? BOARD_WIDTH : endX;
            int halfHeight = BOARD_HEIGHT / 2;
            
            for (int x = startX; x < endX; x++) {
                for (int y = 1; y < halfHeight; y++) {
                    if (random(100) < 20) {
                        board[x][y] = true;
                        board[x][BOARD_HEIGHT - 1 - y] = true;
                    }
                }
            }
            break;
        }
        case 3: {
            int centerX = BOARD_WIDTH / 2;
            int centerY = BOARD_HEIGHT / 2;
            int rangeX = 18 * scale;
            int rangeY = 12 * scale;
            
            for (int x = centerX - rangeX; x < centerX; x++) {
                for (int y = centerY - rangeY; y < centerY; y++) {
                    if (random(100) < 16) {
                        board[x][y] = true;
                        board[BOARD_WIDTH - 1 - x][BOARD_HEIGHT - 1 - y] = true;
                        board[BOARD_WIDTH - 1 - x][y] = true;
                        board[x][BOARD_HEIGHT - 1 - y] = true;
                    }
                }
            }
            break;
        }
    }
}

void initializeDayNight() {
    clearBoard();
    switch(dayNightDensitySelection) {
        case 1:
            for (int x = 0; x < BOARD_WIDTH; x++) {
                for (int y = 0; y < BOARD_HEIGHT; y++) {
                    if (random(100) < 35) {
                        board[x][y] = true;
                        cellAges[x][y] = 1;  // Initialize age
                    }
                }
            }
            break;
        case 2:
            for (int x = 0; x < BOARD_WIDTH; x++) {
                for (int y = 0; y < BOARD_HEIGHT; y++) {
                    if (random(100) < 50) {
                        board[x][y] = true;
                        cellAges[x][y] = 1;  // Initialize age
                    }
                }
            }
            break;
    }
    cellCount = 0;
    generation = 0;
    dayNightMode = true;
}

// Add forward declaration at top of file if needed
extern void drawDayNight();

void updateDayNight() {
    cellCount = 0;
    
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            bool isAlive = board[x][y];
            int neighbors = 0;
            
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    int nx, ny;
                    if (toroidalWorld) {
                        nx = (x + dx + BOARD_WIDTH) % BOARD_WIDTH;
                        ny = (y + dy + BOARD_HEIGHT) % BOARD_HEIGHT;
                    } else {
                        nx = x + dx;
                        ny = y + dy;
                        if (nx < 0 || nx >= BOARD_WIDTH || ny < 0 || ny >= BOARD_HEIGHT) {
                            continue;
                        }
                    }
                    
                    if (board[nx][ny]) {
                        neighbors++;
                    }
                }
            }
            
            bool shouldLive = false;
            if (isAlive) {
                if (neighbors == 3 || neighbors == 4 || neighbors == 6 || neighbors == 7 || neighbors == 8) {
                    shouldLive = true;
                }
            } else {
                if (neighbors == 3 || neighbors == 6 || neighbors == 7 || neighbors == 8) {
                    shouldLive = true;
                }
            }
            
            newBoard[x][y] = shouldLive;
            if (shouldLive) cellCount++;
        }
    }
    
    // Update ages and trails for color mode
    if (dayNightColorMode) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                if (board[x][y] != newBoard[x][y]) {
                    if (newBoard[x][y]) {
                        // Cell born - start at age 1, clear trail
                        cellAges[x][y] = 1;
                        // cellAges stores trail for dead cells in Day&Night
                    } else {
                        // Cell died - reset age, start trail
                        cellAges[x][y] = 0;
                        cellAges[x][y] = 1;  // Start trail (cellAges stores trail for dead cells in Day&Night)
                    }
                } else if (newBoard[x][y] && cellAges[x][y] < 255) {
                    // Cell stayed alive - increment age, no trail
                    cellAges[x][y]++;
                    // cellAges stores trail for dead cells in Day&Night
                } else if (!newBoard[x][y] && cellAges[x][y] > 0 && cellAges[x][y] < 15) {
                    // Continue aging the trail
                    cellAges[x][y]++;
                    if (cellAges[x][y] >= 15) {
                        cellAges[x][y] = 0;  // Trail fully faded
                    }
                }
            }
        }
    }
}

void drawDayNight() {
    static bool needsFullRedraw = true;
    
    // Check if we need a full redraw (after reset or color mode toggle)
    if (needsFullRedraw && showGridLines) {
        drawFullGrid();
        needsFullRedraw = false;
    }
    
    if (dayNightColorMode) {
        // COLOR MODE with trails - redraw ALL cells every frame
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                board[x][y] = newBoard[x][y];
                if (newBoard[x][y]) {
                    // Living cell - warm day colors
                    drawCell(x, y, getDayNightColorByAge(cellAges[x][y], true));
                } else if (cellAges[x][y] > 0 && !newBoard[x][y]) {
                    // Dead cell with trail - cool night colors
                    drawCell(x, y, getDayNightColorByAge(cellAges[x][y], false));
                } else {
                    // Empty space
                    drawCell(x, y, COLOR_BLACK);
                }
            }
        }
    } else {
        // MONO MODE - only redraw changed cells for efficiency
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                if (board[x][y] != newBoard[x][y]) {
                    board[x][y] = newBoard[x][y];
                    drawCell(x, y, newBoard[x][y] ? COLOR_WHITE : COLOR_BLACK);
                }
            }
        }
    }
}

void initializeCyclic() {
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            cyclicStates[x][y] = 0;
            cyclicNewStates[x][y] = 0;
        }
    }
    
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    int patternType = (cyclicSizeSelection == 3) ? random(2) : (cyclicSizeSelection - 1);
    int patternSize = (cyclicSizeSelection == 3) ? (random(2) + 2) : (cyclicSizeSelection + 1);
    int scale = (currentCellSize == 2) ? 2 : 1;
    
    if (patternType == 0) {
        int halfWidth = (patternSize == 2) ? (12 * scale) : (22 * scale);
        for (int x = centerX - halfWidth; x < centerX; x++) {
            if (x < 0) continue;
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                int density = (patternSize == 2) ? 33 : 25;
                if (random(100) < density) {
                    int distFromCenterX = abs(x - centerX);
                    int divisor = (patternSize == 2) ? (3 * scale) : (5 * scale);
                    int state = ((distFromCenterX / divisor) + (y / (6 * scale))) % 5 + 1;
                    int mirrorX = centerX + (centerX - x - 1);
                    if (mirrorX < BOARD_WIDTH) {
                        cyclicStates[x][y] = state;
                        cyclicStates[mirrorX][y] = state;
                    }
                }
            }
        }
    } else {
        for (int x = 0; x < centerX; x++) {
            for (int y = 0; y < centerY; y++) {
                int density = (patternSize == 2) ? 25 : 20;
                if (random(100) < density) {
                    int dx = x - centerX;
                    int dy = y - centerY;
                    int dist = (int)sqrt(dx*dx + dy*dy);
                    int divisor = (patternSize == 2) ? (4 * scale) : (5 * scale);
                    int state = (dist / divisor) % 5 + 1;
                    cyclicStates[x][y] = state;
                    cyclicStates[BOARD_WIDTH - 1 - x][BOARD_HEIGHT - 1 - y] = state;
                    cyclicStates[BOARD_WIDTH - 1 - x][y] = state;
                    cyclicStates[x][BOARD_HEIGHT - 1 - y] = state;
                }
            }
        }
    }
    
    cellCount = 0;
    generation = 0;
    cyclicMode = true;
}

void initializeSeeds() {
    clearBoard();
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    int scale = (currentCellSize == 2) ? 2 : 1;
    int halfWidth = 8 * scale;
    int halfHeight = 4 * scale;
    
    for (int x = centerX - halfWidth; x < centerX; x++) {
        for (int y = centerY - halfHeight; y < centerY; y++) {
            if (random(100) < 8) {
                board[x][y] = true;
                board[x][centerY + (centerY - y - 1)] = true;
                board[centerX + (centerX - x - 1)][y] = true;
                board[centerX + (centerX - x - 1)][centerY + (centerY - y - 1)] = true;
            }
        }
    }
    
    cellCount = 0;
    generation = 0;
    seedsMode = true;
    
    // Initialize ages for color mode
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            seedsAges[x][y] = board[x][y] ? 0 : 0;
        }
    }
}

void updateSeeds() {
    cellCount = 0;
    
    // Calculate new generation
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            int neighbors = 0;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    int nx, ny;
                    if (toroidalWorld) {
                        nx = (x + dx + BOARD_WIDTH) % BOARD_WIDTH;
                        ny = (y + dy + BOARD_HEIGHT) % BOARD_HEIGHT;
                    } else {
                        nx = x + dx;
                        ny = y + dy;
                        if (nx < 0 || nx >= BOARD_WIDTH || ny < 0 || ny >= BOARD_HEIGHT) {
                            continue;
                        }
                    }
                    
                    if (board[nx][ny]) {
                        neighbors++;
                    }
                }
            }
            
            newBoard[x][y] = (neighbors == 2);
            if (newBoard[x][y]) {
                cellCount++;
            }
        }
    }
    
    // Update ages ONLY when color mode is on - only for cells that actually changed
    if (seedsColorMode) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                if (newBoard[x][y]) {
                    // Cell is alive - set age to 0 (brightest white/cyan)
                    seedsAges[x][y] = 0;
                } else if (board[x][y] && !newBoard[x][y]) {
                    // Cell just died - start trail at age 1
                    seedsAges[x][y] = 1;
                } else if (seedsAges[x][y] > 0 && seedsAges[x][y] < 12) {
                    // Continue aging the trail (now 12 steps)
                    seedsAges[x][y]++;
                    if (seedsAges[x][y] >= 12) {
                        seedsAges[x][y] = 0;  // Trail fully faded
                    }
                }
            }
        }
    }
    }

  void drawSeeds() {
    static bool lastBoard[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
    static uint8_t lastAges[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
    static bool firstDraw = true;
    
    if (firstDraw) {
        display.fillScreen(COLOR_BLACK);
        if (showGridLines) {
            drawFullGrid();
        }
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                lastBoard[x][y] = false;
                lastAges[x][y] = 0;
            }
        }
        firstDraw = false;
    }
    
    if (seedsColorMode) {
        // COLOR MODE - Trail/fade effect
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                // Only redraw if there's an actual visual change
                bool stateChanged = (board[x][y] != newBoard[x][y]);
                bool ageChanged = (seedsAges[x][y] != lastAges[x][y]);
                
                // Only update if state changed OR age changed AND age is in visible range
                if (stateChanged || (ageChanged && seedsAges[x][y] <= 11)) {
                    board[x][y] = newBoard[x][y];
                    
                    uint16_t color;
                    uint8_t age = seedsAges[x][y];
                    
                   if (age == 0 && newBoard[x][y]) {
                        color = 0xFFFF;  // Bright White - living cell (birth moment)
                    } else if (age == 1) {
                        color = COLOR_BLACK;  // Black gap for separation
                    } else if (age == 2) {
                        color = 0x07FF;  // Bright Cyan
                    } else if (age == 3) {
                        color = COLOR_BLACK;  // Black gap
                    } else if (age == 4) {
                        color = 0x001F;  // Blue
                    } else if (age == 5) {
                        color = 0x780F;  // Purple
                    } else if (age == 6) {
                        color = 0xF81F;  // Magenta
                    } else if (age == 7) {
                        color = 0xF800;  // Red
                    } else if (age == 8) {
                        color = 0xFD20;  // Orange
                    } else if (age == 9) {
                        color = 0xFFE0;  // Yellow
                    } else if (age == 10) {
                        color = 0x8410;  // Dark yellow
                    } else if (age == 11) {
                        color = 0x2104;  // Nearly black
                    } else {
                        color = COLOR_BLACK;
                    }
                    
                    drawCell(x, y, color);
                    lastBoard[x][y] = newBoard[x][y];
                    lastAges[x][y] = seedsAges[x][y];
                }
            }
        }
    } else {
        // MONO MODE - Original behavior
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                if (board[x][y] != newBoard[x][y]) {
                    board[x][y] = newBoard[x][y];
                    drawCell(x, y, newBoard[x][y] ? COLOR_WHITE : COLOR_BLACK);
                    lastBoard[x][y] = newBoard[x][y];
                }
            }
        }
    }
}

void updateCyclic() {
    cellCount = 0;
    
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            uint8_t currentState = cyclicStates[x][y];
            uint8_t nextState = (currentState + 1) % 6;
            int nextStateNeighbors = 0;
            
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    int nx, ny;
                    if (toroidalWorld) {
                        nx = (x + dx + BOARD_WIDTH) % BOARD_WIDTH;
                        ny = (y + dy + BOARD_HEIGHT) % BOARD_HEIGHT;
                    } else {
                        nx = x + dx;
                        ny = y + dy;
                        if (nx < 0 || nx >= BOARD_WIDTH || ny < 0 || ny >= BOARD_HEIGHT) {
                            continue;
                        }
                    }
                    
                    if (cyclicStates[nx][ny] == nextState) {
                        nextStateNeighbors++;
                    }
                }
            }
            
            if (nextStateNeighbors >= cyclicThreshold) {
                cyclicNewStates[x][y] = nextState;
            } else {
                cyclicNewStates[x][y] = currentState;
            }
            
            if (cyclicNewStates[x][y] > 0) {
                cellCount++;
            }
        }
    }
    
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            cyclicStates[x][y] = cyclicNewStates[x][y];
        }
    }
}

void drawCyclic() {
    static uint8_t lastCyclicStates[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
    static bool lastFastBlink = false;
    static bool lastSlowBlink = false;
    static bool lastAltPixel = false;
    static bool firstDraw = true;
    
    if (firstDraw) {
        display.fillScreen(COLOR_BLACK);
        if (showGridLines) {
            drawFullGrid();
        }
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                lastCyclicStates[x][y] = 0;
            }
        }
        firstDraw = false;
    }
    
    if (cyclicColorMode) {
        // COLOR MODE - Rainbow cycle
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                uint8_t state = cyclicStates[x][y];
                
                if (state != lastCyclicStates[x][y]) {
                    uint16_t color;
                    switch(state) {
                        case 0: color = COLOR_BLACK; break;
                        case 1: color = 0xF800; break;  // Red
                        case 2: color = 0xFD20; break;  // Orange
                        case 3: color = 0x07E0; break;  // Green
                        case 4: color = 0x07FF; break;  // Cyan
                        case 5: color = 0xF81F; break;  // Magenta
                    }
                    drawCell(x, y, color);
                    lastCyclicStates[x][y] = state;
                }
            }
        }
    } else {
        // MONO MODE - Original blinking behavior
        unsigned long t = millis();
        bool fastBlink = (t / 100) % 2;
        bool slowBlink = (t / 300) % 2;
        bool altPixel = (t / 200) % 2;
        
        bool blinkChanged = (fastBlink != lastFastBlink) || 
                            (slowBlink != lastSlowBlink) || 
                            (altPixel != lastAltPixel);
        
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                uint8_t state = cyclicStates[x][y];
                bool needsRedraw = (state != lastCyclicStates[x][y]) || 
                                  (blinkChanged && (state == 2 || state == 3 || state == 4));
                
                if (needsRedraw) {
                    display.fillRect(x * currentCellSize, y * currentCellSize, currentCellSize, currentCellSize, COLOR_BLACK);
                    
                    switch(state) {
                        case 0: break;
                        case 1:
                            display.fillRect(x * currentCellSize, y * currentCellSize, 2, 2, COLOR_WHITE);
                            break;
                        case 2:
                            if (fastBlink) {
                                display.fillRect(x * currentCellSize, y * currentCellSize, 2, 2, COLOR_WHITE);
                            }
                            break;
                        case 3:
                            if (slowBlink) {
                                display.fillRect(x * currentCellSize, y * currentCellSize, 2, 2, COLOR_WHITE);
                            }
                            break;
                        case 4:
                            if (altPixel) {
                                display.fillRect(x * currentCellSize, y * currentCellSize, 1, 1, COLOR_WHITE);
                            } else {
                                display.fillRect(x * currentCellSize + 1, y * currentCellSize + 1, 1, 1, COLOR_WHITE);
                            }
                            break;
                        case 5:
                            display.fillRect(x * currentCellSize, y * currentCellSize, currentCellSize, currentCellSize, COLOR_WHITE);
                            break;
                    }
                    lastCyclicStates[x][y] = state;
                }
            }
        }
        
        lastFastBlink = fastBlink;
        lastSlowBlink = slowBlink;
        lastAltPixel = altPixel;
    }
}

void updateBriansBrain() {
    cellCount = 0;
    
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            bool isAlive = board[x][y];
            bool isDying = briansBrain_dying[x][y];
            int neighbors = 0;
            
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
                    
                    int nx, ny;
                    if (toroidalWorld) {
                        nx = (x + dx + BOARD_WIDTH) % BOARD_WIDTH;
                        ny = (y + dy + BOARD_HEIGHT) % BOARD_HEIGHT;
                    } else {
                        nx = x + dx;
                        ny = y + dy;
                        if (nx < 0 || nx >= BOARD_WIDTH || ny < 0 || ny >= BOARD_HEIGHT) {
                            continue;
                        }
                    }
                    
                    if (board[nx][ny] && !briansBrain_dying[nx][ny]) {
                        neighbors++;
                    }
                }
            }
            
            if (isAlive && !isDying) {
                newBoard[x][y] = false;
                briansBrain_newDying[x][y] = true;
                cellCount++;
            } else if (!isAlive && !isDying && neighbors == 2) {
                newBoard[x][y] = true;
                briansBrain_newDying[x][y] = false;
                cellCount++;
            } else {
                newBoard[x][y] = false;
                briansBrain_newDying[x][y] = false;
            }
        }
    }
    
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            board[x][y] = newBoard[x][y];
            briansBrain_dying[x][y] = briansBrain_newDying[x][y];
        }
    }
    
    if (cellCount > max_cellCount) {
        max_cellCount = cellCount;
    }
}

void drawBriansBrain() {
    static bool lastBoard[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
    static bool lastDying[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
    static uint8_t trailAge[MAX_BOARD_WIDTH][MAX_BOARD_HEIGHT];
    static bool lastBlinkState = false;
    static bool firstDraw = true;
    static int lastBoardWidth = 0;
    static int lastBoardHeight = 0;
    static uint32_t lastGeneration = 0;
    
    bool blinkState = (millis() / 100) % 2;
    bool blinkChanged = (blinkState != lastBlinkState);
    
    // Reset if board dimensions changed (pixel size changed)
    if (BOARD_WIDTH != lastBoardWidth || BOARD_HEIGHT != lastBoardHeight) {
        firstDraw = true;
        lastBoardWidth = BOARD_WIDTH;
        lastBoardHeight = BOARD_HEIGHT;
    }
    
    // Reset if generation counter reset (indicates a reset/restart)
    if (generation < lastGeneration) {
        firstDraw = true;
    }
    lastGeneration = generation;
    
    if (firstDraw) {
        display.fillScreen(COLOR_BLACK);
        if (showGridLines) {
            drawFullGrid();
        }
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                lastBoard[x][y] = false;
                lastDying[x][y] = false;
                trailAge[x][y] = 0;
            }
        }
        firstDraw = false;
    }
    
    // Update trail ages in color mode
    if (briansBrainColorMode) {
        for (int x = 0; x < BOARD_WIDTH; x++) {
            for (int y = 0; y < BOARD_HEIGHT; y++) {
                bool isAlive = board[x][y];
                bool isDying = briansBrain_dying[x][y];
                
                if (isAlive && !isDying) {
                    // Active neuron - reset trail
                    trailAge[x][y] = 0;
                } else if (isDying) {
                    // Just fired - start trail at age 1
                    if (trailAge[x][y] == 0) {
                        trailAge[x][y] = 1;
                    } else if (trailAge[x][y] < 20) {
                        trailAge[x][y]++;
                    }
                } else if (trailAge[x][y] > 0 && trailAge[x][y] < 20) {
                    // Continue aging the trail even after cell is dead
                    trailAge[x][y]++;
                } else if (trailAge[x][y] >= 20) {
                    // Trail fully faded
                    trailAge[x][y] = 0;
                }
            }
        }
    }
    
    for (int x = 0; x < BOARD_WIDTH; x++) {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            bool isAlive = board[x][y];
            bool isDying = briansBrain_dying[x][y];
            bool hasTrail = (trailAge[x][y] > 0);
            
            if (briansBrainColorMode) {
                // COLOR MODE - always redraw cells with trails
                bool needsRedraw = (isAlive != lastBoard[x][y]) || 
                                  (isDying != lastDying[x][y]) || 
                                  hasTrail;
                
                if (needsRedraw) {
                    if (isAlive && !isDying) {
                        // Alive = Bright cyan (firing neuron)
                        drawCell(x, y, 0x07FF);
                    } else if (isDying || hasTrail) {
                        // Dying or trail - gradient based on age
                        uint16_t color;
                        uint8_t age = trailAge[x][y];
                        
                        if (age <= 3) {
                            color = 0xFFFF;  // Bright white
                        } else if (age <= 6) {
                            color = 0xFFE0;  // Yellow
                        } else if (age <= 9) {
                            color = 0xFD20;  // Orange
                        } else if (age <= 12) {
                            color = 0xF800;  // Red
                        } else if (age <= 15) {
                            color = 0x780F;  // Purple
                        } else if (age <= 18) {
                            color = 0x4008;  // Dark purple
                        } else {
                            color = COLOR_BLACK;  // Faded
                        }
                        
                        drawCell(x, y, color);
                    } else {
                        // Dead with no trail
                        drawCell(x, y, COLOR_BLACK);
                    }
                }
            } else {
                // MONO MODE - original blinking behavior
                bool stateChanged = (isAlive != lastBoard[x][y]) || (isDying != lastDying[x][y]);
                bool needsRedraw = stateChanged || (isDying && blinkChanged);
                
                if (needsRedraw) {
                    display.fillRect(x * currentCellSize, y * currentCellSize, currentCellSize, currentCellSize, COLOR_BLACK);
                    
                    if (isAlive && !isDying) {
                        display.fillRect(x * currentCellSize, y * currentCellSize, currentCellSize, currentCellSize, COLOR_WHITE);
                    } else if (isDying && blinkState) {
                        display.fillRect(x * currentCellSize, y * currentCellSize, currentCellSize, currentCellSize, COLOR_WHITE);
                    }
                }
            }
            
            lastBoard[x][y] = isAlive;
            lastDying[x][y] = isDying;
        }
    }
    
    lastBlinkState = blinkState;
}

void updateMenuSelection(int oldSelection, int newSelection) {
    // Clear the entire menu area below the title to avoid artifacts
    display.fillRect(0, 50, SCREEN_WIDTH, 190, COLOR_BLACK);
    
    display.setTextSize(2);
    int yPos = 50;
    
    // Determine how many items based on which menu we're in
int numItems = 7; // Default for main menu
if (inToolsSubMenu) numItems = 6;  // Changed from 3 to 6
    else if (inSymmetricSubMenu) numItems = 3;
    else if (inAltGamesSubMenu) numItems = 4;
    else if (inBriansBrainSubMenu) numItems = 5;
    else if (inCyclicSubMenu) numItems = 3;
    else if (inDayNightSubMenu) numItems = 2;
    else if (inSeedsSubMenu) numItems = 2;
    else if (inCellSizeSubMenu) numItems = 4;
    else if (inPatternsSubMenu) numItems = 5;
    
    // Redraw all menu items
    for (int i = 1; i <= numItems; i++) {
        if (i == newSelection) {
            display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
            display.setTextColor(COLOR_BLACK);
        } else {
            display.setTextColor(COLOR_WHITE);
        }
        
        display.setCursor(10, yPos + 5);
        
        // Draw the appropriate text based on menu type
        if (inToolsSubMenu) {
            if (i == 1) {
                display.print("Sound: ");
                display.println(soundEnabled ? "ON" : "OFF");
            } else if (i == 2) {
                display.print("Volume: ");
                switch(soundVolume) {
                    case 1: display.println("Low"); break;
                    case 2: display.println("Medium"); break;
                    case 3: display.println("High"); break;
                }
            } else if (i == 3) {
                display.print("World: ");
                display.println(toroidalWorld ? "Toroid" : "Open");
            } else if (i == 4) {
                display.print("Grid: ");
                display.println(showGridLines ? "ON" : "OFF");
            } else if (i == 5) {
                display.print("Population: ");
                display.println(showPopCounter ? "ON" : "OFF");
            } else if (i == 6) {
                display.print("Trail Mode: ");
                display.println(trailMode ? "ON" : "OFF");
            }
        }else if (inSymmetricSubMenu) {
            if (i == 1) display.println("Small");
            else if (i == 2) display.println("Medium");
            else display.println("Large");
        } else if (inAltGamesSubMenu) {
            if (i == 1) display.println("Brian's Brain");
            else if (i == 2) display.println("Day & Night");
            else if (i == 3) display.println("Seeds");
            else display.println("Cyclic");
        } else if (inBriansBrainSubMenu) {
            if (i == 1) display.println("Small");
            else if (i == 2) display.println("Medium");
            else if (i == 3) display.println("Large");
            else if (i == 4) display.println("Random");
            else display.println("Custom");
        } else if (inCyclicSubMenu) {
            if (i == 1) display.println("Vertical");
            else if (i == 2) display.println("4-Way Rotate");
            else display.println("Random");
        } else if (inDayNightSubMenu) {
            if (i == 1) display.println("Medium");
            else display.println("Dense");
        } else if (inSeedsSubMenu) {
            if (i == 1) display.println("Random");
            else display.println("Custom");
        } else if (inCellSizeSubMenu) {
            if (i == 1) display.println("Tiny");
            else if (i == 2) display.println("Small");
            else if (i == 3) display.println("Normal");
            else display.println("Large");
       } else if (inPatternsSubMenu) {
            if (i == 1) display.println("Coe");
            else if (i == 2) display.println("Gosper");
            else if (i == 3) display.println("Diamond");
            else if (i == 4) display.println("Pulsar");
            else display.println("Glider");
        } else {
            // Main menu
            if (i == 1) display.println("Presets");
            else if (i == 2) display.println("Random");
            else if (i == 3) display.println("Symmetric");
            else if (i == 4) display.println("Custom");
            else if (i == 5) display.println("Rule Explorer");
            else if (i == 6) display.println("Alternative Games");
            else if (i == 7) display.println("Tools");
        }
        
        yPos += 28;
    }
}

void generateBriansBrainSmall() {
    clearBoard();
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    
    // Small cluster - more cells, lower density for wave propagation
    int size = (currentCellSize == 2) ? 12 : 8;  // Larger area
    
    int symmetryType = random(3) + 1;
    
    switch(symmetryType) {
        case 1: // Vertical symmetry
            for (int x = centerX - size; x < centerX; x++) {
                for (int y = centerY - size; y <= centerY + size; y++) {
                    if (random(100) < 35) {  // Lower density - 35% instead of 60%
                        board[x][y] = true;
                        board[centerX + (centerX - x - 1)][y] = true;
                    }
                }
            }
            break;
            
        case 2: // Horizontal symmetry
            for (int x = centerX - size; x <= centerX + size; x++) {
                for (int y = centerY - size; y < centerY; y++) {
                    if (random(100) < 35) {  // Lower density
                        board[x][y] = true;
                        board[x][centerY + (centerY - y - 1)] = true;
                    }
                }
            }
            break;
            
        case 3: // Rotational symmetry
            for (int x = centerX - size; x < centerX; x++) {
                for (int y = centerY - size; y < centerY; y++) {
                    if (random(100) < 30) {  // Lower density for 4-way
                        board[x][y] = true;
                        board[centerX + (centerX - x - 1)][centerY + (centerY - y - 1)] = true;
                        board[centerX + (centerX - x - 1)][y] = true;
                        board[x][centerY + (centerY - y - 1)] = true;
                    }
                }
            }
            break;
    }
}

void generateBriansBrainMedium() {
    clearBoard();
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    
    // Medium cluster - lower density for better wave propagation
    int size = (currentCellSize == 2) ? 20 : 15;  // Larger area
    
    int symmetryType = random(3) + 1;
    
    switch(symmetryType) {
        case 1: // Vertical symmetry
            for (int x = centerX - size; x < centerX; x++) {
                for (int y = centerY - size; y <= centerY + size; y++) {
                    if (random(100) < 28) {  // MUCH lower - 28% instead of 55%
                        board[x][y] = true;
                        board[centerX + (centerX - x - 1)][y] = true;
                    }
                }
            }
            break;
            
        case 2: // Horizontal symmetry
            for (int x = centerX - size; x <= centerX + size; x++) {
                for (int y = centerY - size; y < centerY; y++) {
                    if (random(100) < 28) {  // MUCH lower
                        board[x][y] = true;
                        board[x][centerY + (centerY - y - 1)] = true;
                    }
                }
            }
            break;
            
        case 3: // Rotational symmetry
            for (int x = centerX - size; x < centerX; x++) {
                for (int y = centerY - size; y < centerY; y++) {
                    if (random(100) < 25) {  // MUCH lower for 4-way
                        board[x][y] = true;
                        board[centerX + (centerX - x - 1)][centerY + (centerY - y - 1)] = true;
                        board[centerX + (centerX - x - 1)][y] = true;
                        board[x][centerY + (centerY - y - 1)] = true;
                    }
                }
            }
            break;
    }
}

void generateBriansBrainLarge() {
    clearBoard();
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    
    // Large cluster - sparse for long-lasting patterns
    int size = (currentCellSize == 2) ? 40 : 28;  // Very large area
    
    int symmetryType = random(3) + 1;
    
    switch(symmetryType) {
        case 1: // Vertical symmetry
            for (int x = centerX - size; x < centerX; x++) {
                for (int y = centerY - size; y <= centerY + size; y++) {
                    if (random(100) < 18) {  // MUCH lower - 18% instead of 65%
                        board[x][y] = true;
                        board[centerX + (centerX - x - 1)][y] = true;
                    }
                }
            }
            break;
            
        case 2: // Horizontal symmetry
            for (int x = centerX - size; x <= centerX + size; x++) {
                for (int y = centerY - size; y < centerY; y++) {
                    if (random(100) < 18) {  // MUCH lower
                        board[x][y] = true;
                        board[x][centerY + (centerY - y - 1)] = true;
                    }
                }
            }
            break;
            
        case 3: // Rotational symmetry
            for (int x = centerX - size; x < centerX; x++) {
                for (int y = centerY - size; y < centerY; y++) {
                    if (random(100) < 15) {  // MUCH lower for 4-way
                        board[x][y] = true;
                        board[centerX + (centerX - x - 1)][centerY + (centerY - y - 1)] = true;
                        board[centerX + (centerX - x - 1)][y] = true;
                        board[x][centerY + (centerY - y - 1)] = true;
                    }
                }
            }
            break;
    }
}

void generateBriansBrainRandom() {
    clearBoard();
    int centerX = BOARD_WIDTH / 2;
    int centerY = BOARD_HEIGHT / 2;
    
    // Random scattered - denser in center
    int radius = (currentCellSize == 2) ? 15 : 10;
    
    for (int x = centerX - radius; x <= centerX + radius; x++) {
        for (int y = centerY - radius; y <= centerY + radius; y++) {
            if (x >= 0 && x < BOARD_WIDTH && y >= 0 && y < BOARD_HEIGHT) {
                // Distance from center
                int dx = x - centerX;
                int dy = y - centerY;
                float dist = sqrt(dx*dx + dy*dy);
                
                // Denser near center, sparser at edges
                int probability = (int)(25 * (1.0 - (dist / radius)));
                if (probability > 0 && random(100) < probability) {
                    board[x][y] = true;
                }
            }
        }
    }
}

void showRuleExplorerMenu() {
    display.fillScreen(COLOR_BLACK);
    display.setTextSize(2);
    display.setTextColor(COLOR_WHITE);
    display.setCursor(10, 5);
    
    if (inCustomRuleEdit) {
        display.println("CUSTOM RULE");
        display.setTextSize(1);
        display.println();
        
        // Show current birth rule
        display.setCursor(10, 35);
        display.print("Birth: ");
        for (int i = 0; i <= 8; i++) {
            if (birthRule & (1 << i)) {
                display.print(i);
            }
        }
        
        // Draw birth selection row
        int yPos = 60;
        if (customEditRow == 0) {
            display.fillRect(0, yPos - 5, SCREEN_WIDTH, 30, 0x2104);
        }
        display.setTextSize(2);
        for (int i = 0; i <= 8; i++) {
            int xPos = 10 + i * 32;
            
            if (i == customEditCursor && customEditRow == 0) {
                display.fillRect(xPos - 2, yPos - 2, 28, 24, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            
            display.setCursor(xPos + 2, yPos);
            if (birthRule & (1 << i)) {
                display.print((char)('0' + i));
            } else {
                display.print(".");
            }
        }
        
        // Show current survival rule
        display.setTextSize(1);
        display.setTextColor(COLOR_WHITE);
        display.setCursor(10, 105);
        display.print("Survival: ");
        for (int i = 0; i <= 8; i++) {
            if (survivalRule & (1 << i)) {
                display.print(i);
            }
        }
        
        // Draw survival selection row
        yPos = 130;
        if (customEditRow == 1) {
            display.fillRect(0, yPos - 5, SCREEN_WIDTH, 30, 0x2104);
        }
        display.setTextSize(2);
        for (int i = 0; i <= 8; i++) {
            int xPos = 10 + i * 32;
            
            if (i == customEditCursor && customEditRow == 1) {
                display.fillRect(xPos - 2, yPos - 2, 28, 24, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            
            display.setCursor(xPos + 2, yPos);
            if (survivalRule & (1 << i)) {
                display.print((char)('0' + i));
            } else {
                display.print(".");
            }
        }
        
        // Instructions
        display.setTextColor(COLOR_WHITE);
        display.setTextSize(1);
        display.setCursor(10, 170);
        display.println("UP/DN: Row  L/R: Move");
        display.setCursor(10, 185);
        display.println("SET: Toggle");
        display.setCursor(10, 200);
        display.println("HOLD SET: Start Game");
        display.setCursor(10, 215);
        display.println("B: Back");
        
    } else {
        display.println("RULE EXPLORER");
        display.println();
        
        display.setTextSize(2);
        int yPos = 50;
        int maxVisibleItems = 6;  // Maximum items that fit on screen
        int totalItems = NUM_PRESET_RULES + 1;  // Presets + Custom
        
        // Calculate scroll offset to keep selected item visible
        int scrollOffset = 0;
        if (ruleMenuSelection > maxVisibleItems) {
            scrollOffset = ruleMenuSelection - maxVisibleItems;
        }
        
        // Draw visible items
        for (int i = 0; i < maxVisibleItems && (i + scrollOffset) < totalItems; i++) {
            int itemIndex = i + scrollOffset + 1;  // +1 because menu is 1-indexed
            
            if (ruleMenuSelection == itemIndex) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 25, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            
            display.setCursor(10, yPos + 3);
            if (itemIndex <= NUM_PRESET_RULES) {
                // Read from PROGMEM
                char nameBuf[12];
                strcpy_P(nameBuf, PRESET_RULES[itemIndex - 1].name);
                display.println(nameBuf);
            } else {
                display.println("Custom Rule");
            }
            
            yPos += 25;
        }
        
        // Show scroll indicator if needed
        if (totalItems > maxVisibleItems) {
            display.setTextSize(3);  // Larger size
            display.setTextColor(COLOR_WHITE);
            
            // Up arrow
            if (scrollOffset > 0) {
                display.setCursor(SCREEN_WIDTH - 25, 10);
                display.print("^");
            }
            
            // Down arrow
            if (scrollOffset + maxVisibleItems < totalItems) {
                display.setCursor(SCREEN_WIDTH - 25, SCREEN_HEIGHT - 30);
                display.print("v");
            }
        }
    }
}

void showMenu() {
    display.fillScreen(COLOR_BLACK);
    display.setTextSize(3);
    display.setTextColor(COLOR_WHITE);
    display.setCursor(10, 5);
    
    if (inToolsSubMenu) {
        display.println("TOOLS");
    } else if (inSymmetricSubMenu) {
        display.println("SYMMETRIC");
    } else if (inPatternsSubMenu) {
        display.println("PRESETS");
    } else if (inBriansBrainSubMenu) {
    display.println("BRIAN'S BRAIN");
    } else if (inSeedsSubMenu) {
        display.println("SEEDS");
    } else if (inCyclicSubMenu) {
        display.println("CYCLIC CA");
    } else if (inDayNightSubMenu) {
        display.println("DAY&NIGHT");
    } else if (inAltGamesSubMenu) {
        display.println("ALTERNATIVE GAMES");
    } else if (inCellSizeSubMenu) {
        display.println("CELL SIZE");
    } else {
        display.println("GAME OF LIFE");
    }
    
    display.setTextSize(2);  // Increased from 1 for menu items

// Use consistent spacing for all menus
int yPos = 50;
    
    if (inSeedsSubMenu) {
        for (int i = 1; i <= 2; i++) {
            if (seedsGameSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            if (i == 1) display.println("Random");
            else display.println("Custom");
            yPos += 28;
        }
    } else if (inToolsSubMenu) {
        for (int i = 1; i <= 6; i++) {
            if (toolsSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            if (i == 1) {
                display.print("Sound: ");
                display.println(soundEnabled ? "ON" : "OFF");
            } else if (i == 2) {
                display.print("Volume: ");
                switch(soundVolume) {
                    case 1: display.println("Low"); break;
                    case 2: display.println("Medium"); break;
                    case 3: display.println("High"); break;
                }
            } else if (i == 3) {
                display.print("World: ");
                display.println(toroidalWorld ? "Toroid" : "Open");
            } else if (i == 4) {
                display.print("Grid: ");
                display.println(showGridLines ? "ON" : "OFF");
            } else if (i == 5) {
                display.print("Population: ");
                display.println(showPopCounter ? "ON" : "OFF");
            } else {
                display.print("Trail Mode: ");
                display.println(trailMode ? "ON" : "OFF");
            }
            yPos += 28;
        }
    } else if (inSymmetricSubMenu) {
        for (int i = 1; i <= 3; i++) {
            if (symmetricSizeSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            if (i == 1) display.println("Small");
            else if (i == 2) display.println("Medium");
            else display.println("Large");
            yPos += 28;
        }
    } else if (inPatternsSubMenu) {
        for (int i = 1; i <= 5; i++) {
            if (patternSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            switch(i) {
                case 1: display.println("Coe"); break;
                case 2: display.println("Gosper"); break;
                case 3: display.println("Diamond"); break;
                case 4: display.println("Pulsar"); break;
                case 5: display.println("Glider"); break;
            }
            yPos += 28;
        }
    } else if (inAltGamesSubMenu) {
    for (int i = 1; i <= 4; i++) {
        if (altGameSelection == i) {
            display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
            display.setTextColor(COLOR_BLACK);
        } else {
            display.setTextColor(COLOR_WHITE);
        }
        display.setCursor(10, yPos + 5);
        switch(i) {
            case 1: display.println("Brian's Brain"); break;
            case 2: display.println("Day & Night"); break;
            case 3: display.println("Seeds"); break;
            case 4: display.println("Cyclic"); break;
        }
        yPos += 28;
    }

    } else if (inBriansBrainSubMenu) {
        for (int i = 1; i <= 5; i++) {
            if (briansBrainSizeSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            switch(i) {
                case 1: display.println("Small"); break;
                case 2: display.println("Medium"); break;
                case 3: display.println("Large"); break;
                case 4: display.println("Random"); break;
                case 5: display.println("Custom"); break;
            }
            yPos += 28;
        }
    } else if (inCyclicSubMenu) {
        for (int i = 1; i <= 3; i++) {
            if (cyclicSizeSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            switch(i) {
                case 1: display.println("Vertical"); break;
                case 2: display.println("4-Way Rotate"); break;
                case 3: display.println("Random"); break;
            }
            yPos += 28;
        }
    } else if (inDayNightSubMenu) {
        for (int i = 1; i <= 2; i++) {
            if (dayNightDensitySelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            if (i == 1) display.println("Medium");
            else display.println("Dense");
            yPos += 28;
        }
} else if (!inCellSizeSubMenu) {
        for (int i = 1; i <= 7; i++) {
            if (menuSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            switch(i) {
                case 1: display.println("Presets"); break;
                case 2: display.println("Random"); break;
                case 3: display.println("Symmetric"); break;
                case 4: display.println("Custom"); break;
                case 5: display.println("Rule Explorer"); break;
                case 6: display.println("Alternative Games"); break;
                case 7: display.println("Tools"); break;
            }
            yPos += 28;
        }
    } else if (inCellSizeSubMenu) {
        for (int i = 1; i <= 4; i++) {
            if (cellSizeSelection == i) {
                display.fillRect(0, yPos, SCREEN_WIDTH, 30, COLOR_WHITE);
                display.setTextColor(COLOR_BLACK);
            } else {
                display.setTextColor(COLOR_WHITE);
            }
            display.setCursor(10, yPos + 5);
            switch(i) {
                case 1: display.println("Tiny"); break;
                case 2: display.println("Small"); break;
                case 3: display.println("Normal"); break;
                case 4: display.println("Large"); break;
            }
            yPos += 28;
        }
    }
}

    void handleRuleExplorerButtons() {
    static unsigned long lastButtonPress = 0;
    unsigned long now = millis();
    
    if (now - lastButtonPress < 150) return;
    
    if (inCustomRuleEdit) {
        // Custom rule editor
        if (isBtnUp()) {
            customEditRow = 1 - customEditRow;
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
        if (isBtnDown()) {
            customEditRow = 1 - customEditRow;
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
        if (isBtnLeft()) {
            if (customEditCursor > 0) customEditCursor--;
            else customEditCursor = 8;
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
        if (isBtnRight()) {
            if (customEditCursor < 8) customEditCursor++;
            else customEditCursor = 0;
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
        static bool setButtonPressedInEditor = false;
        static unsigned long setButtonPressStartInEditor = 0;
        
        if (isBtnSet() && !setButtonPressedInEditor) {
            setButtonPressedInEditor = true;
            setButtonPressStartInEditor = now;
        }
        
        if (isBtnSet() && setButtonPressedInEditor) {
            unsigned long pressDuration = now - setButtonPressStartInEditor;
            
            // Long press (800ms) = start game
            if (pressDuration >= 800) {
                display.fillScreen(COLOR_BLACK);  // Clear menu
                startedFromCustomRule = true;  // Mark that we started from custom editor
                ruleExplorerMode = true;
                inRuleExplorerMenu = false;
                inCustomRuleEdit = false;
                gameMode = 1;
                randomizeBoard();
                editMode = false;
                inMenuMode = false;
                generation = 0;
                cellCount = 0;
                max_cellCount = 0;
                start_t = micros();
                frame_start_t = millis();
                playGameStart();
                setButtonPressedInEditor = false;
                lastButtonPress = now;
                return;
            }
        }
        
        if (!isBtnSet() && setButtonPressedInEditor) {
            // Short press = toggle bit
            if (customEditRow == 0) {
                birthRule ^= (1 << customEditCursor);
            } else {
                survivalRule ^= (1 << customEditCursor);
            }
            setButtonPressedInEditor = false;
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
        if (isBtnB()) {
            inCustomRuleEdit = false;
            ruleMenuSelection = NUM_PRESET_RULES + 1;  // Set selection back to "Custom..."
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
    } else {
        // Preset menu
        if (isBtnUp()) {
            ruleMenuSelection--;
            if (ruleMenuSelection < 1) ruleMenuSelection = NUM_PRESET_RULES + 1;
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
        if (isBtnDown()) {
            ruleMenuSelection++;
            if (ruleMenuSelection > NUM_PRESET_RULES + 1) ruleMenuSelection = 1;
            lastButtonPress = now;
            playMenuBeep();
            showRuleExplorerMenu();
        }
        
        if (isBtnSet()) {
            if (ruleMenuSelection <= NUM_PRESET_RULES) {
                // Load preset rule from PROGMEM
                birthRule = pgm_read_byte(&PRESET_RULES[ruleMenuSelection - 1].birth);
                survivalRule = pgm_read_byte(&PRESET_RULES[ruleMenuSelection - 1].survival);
                
                // Start game with this rule
                display.fillScreen(COLOR_BLACK);  // Clear menu
                 startedFromCustomRule = false;  // Mark that we started from preset
                ruleExplorerMode = true;
                inRuleExplorerMenu = false;
                gameMode = 1;
                randomizeBoard();
                editMode = false;
                inMenuMode = false;
                generation = 0;
                cellCount = 0;
                max_cellCount = 0;
                start_t = micros();
                frame_start_t = millis();
                playGameStart();
            } else {
                // Custom rule editor
                inCustomRuleEdit = true;
                customEditRow = 0;
                customEditCursor = 3;
                showRuleExplorerMenu();
            }
            lastButtonPress = now;
        }
        
        if (isBtnB()) {
            inRuleExplorerMenu = false;
            inMenuMode = true;
            menuSelection = 5;  // Rule Explorer is now position 5
            display.fillScreen(COLOR_BLACK);
            showMenu();
            lastButtonPress = now;
            playMenuBeep();
        }
    }
}

void showGameRules() {
    // Semi-transparent dark overlay - larger for bigger text
    display.fillRect(10, 20, 300, 200, 0x2104);
    display.drawRect(10, 20, 300, 200, COLOR_WHITE);
    display.drawRect(11, 21, 298, 198, COLOR_WHITE);
    
    display.setTextSize(2);
    display.setTextColor(COLOR_WHITE);
    display.setCursor(20, 30);
    
    if (ruleExplorerMode) {
        display.println(F("RULE EXPLORER"));
        display.setCursor(20, 60);
        display.print(F("Birth: "));
        bool firstBirth = true;
        for (int i = 0; i <= 8; i++) {
            if (birthRule & (1 << i)) {
                if (!firstBirth) display.print(F(","));
                display.print(i);
                firstBirth = false;
            }
        }
        display.setCursor(20, 90);
        display.print(F("Live: "));
        bool firstSurv = true;
        for (int i = 0; i <= 8; i++) {
            if (survivalRule & (1 << i)) {
                if (!firstSurv) display.print(F(","));
                display.print(i);
                firstSurv = false;
            }
        }
        display.setCursor(20, 120);
        display.print(F("Death: "));
        bool firstDeath = true;
        for (int i = 0; i <= 8; i++) {
            // Cell dies if it doesn't survive with this neighbor count
            if (!(survivalRule & (1 << i))) {
                if (!firstDeath) display.print(F(","));
                display.print(i);
                firstDeath = false;
            }
        }
    } else if (briansBrainMode) {
        display.println(F("BRIAN'S BRAIN"));
        display.setCursor(20, 60);
        display.println(F("Birth: 2 neighbours"));
        display.setCursor(20, 90);
        display.println(F("Live: 1 generations"));
        display.setCursor(20, 120);
        display.println(F("Death: always"));
    } else if (dayNightMode) {
        display.println(F("DAY & NIGHT"));
        display.setCursor(20, 60);
        display.println(F("Birth: 3,6,7,8"));
        display.setCursor(20, 90);
        display.println(F("Live: 3,4,6,7,8"));
        display.setCursor(20, 120);
        display.println(F("Death: 0,1,2,5"));
    } else if (seedsMode) {
        display.println(F("SEEDS"));
        display.setCursor(20, 60);
        display.println(F("Birth: 2 neighbours"));
        display.setCursor(20, 90);
        display.println(F("Live: 0 generations"));
        display.setCursor(20, 120);
        display.println(F("Death: instant"));
    } else if (cyclicMode) {
        display.println(F("CYCLIC CA"));
        display.setCursor(20, 60);
        display.println(F("States: 0-5"));
        display.setCursor(20, 90);
        display.print(F("Threshold: "));
        display.println(cyclicThreshold);
        display.setCursor(20, 120);
        display.println(F("Cycle states"));
    } else if (gameMode >= 1 && gameMode <= 4) {
        display.println(F("CONWAY'S LIFE"));
        display.setCursor(20, 60);
        display.println(F("Birth: 3"));
        display.setCursor(20, 90);
        display.println(F("Live: 2,3"));
        display.setCursor(20, 120);
        display.println(F("Death: 0,1,4,5,6,7,8"));
    }
    
    display.setCursor(20, 160);
    display.println(F("HOLD B: close"));
}
       
void handleDirectionalButtons() {
    static unsigned long lastUpPress = 0;
    static unsigned long lastDownPress = 0;
    static unsigned long lastLeftPress = 0;
    static unsigned long lastRightPress = 0;
    static unsigned long lastColorToggle = 0;
    
    unsigned long now = millis();
    
    // CHECK FOR COLOR MODE TOGGLE (UP + DOWN together)
    if (!inMenuMode && !editMode && (gameMode >= 1 && gameMode <= 4)) {
        if (isBtnUp() && isBtnDown() && (now - lastColorToggle >= 500)) {
            lastColorToggle = now;
            colorMode = !colorMode;
            playColorToggle(colorMode);
            
            // Redraw entire board with new color scheme
            display.fillScreen(COLOR_BLACK);
            if (showGridLines) {
                drawFullGrid();
            }
            for (int x = 0; x < BOARD_WIDTH; x++) {
                for (int y = 0; y < BOARD_HEIGHT; y++) {
                    if (board[x][y]) {
                        if (colorMode) {
                            drawCell(x, y, getColorByAge(cellAges[x][y]));
                        } else {
                            drawCell(x, y, COLOR_WHITE);
                        }
                    }
                }
            }

            return;  // Don't process other buttons this frame
        }
    }

    // CHECK FOR CYCLIC COLOR MODE TOGGLE (UP + DOWN together)
    if (!inMenuMode && !editMode && cyclicMode) {
        if (isBtnUp() && isBtnDown() && (now - lastColorToggle >= 500)) {
            lastColorToggle = now;
            cyclicColorMode = !cyclicColorMode;
             playColorToggle(cyclicColorMode);
            
            // Redraw entire board
            display.fillScreen(COLOR_BLACK);
            if (showGridLines) {
                drawFullGrid();
            }
            drawCyclic();

            return;  // Don't process other buttons this frame
        }
    }
    
    // CHECK FOR SEEDS COLOR MODE TOGGLE (UP + DOWN together)
    if (!inMenuMode && !editMode && seedsMode) {
        if (isBtnUp() && isBtnDown() && (now - lastColorToggle >= 500)) {
            lastColorToggle = now;
            seedsColorMode = !seedsColorMode;
            playColorToggle(seedsColorMode);
            
            // Redraw entire board
            display.fillScreen(COLOR_BLACK);
            if (showGridLines) {
                drawFullGrid();
            }
            drawSeeds();
    
            return;  // Don't process other buttons this frame
        }
    }
    
  // CHECK FOR DAY & NIGHT COLOR MODE TOGGLE (UP + DOWN together)
    if (!inMenuMode && !editMode && dayNightMode) {
        if (isBtnUp() && isBtnDown() && (now - lastColorToggle >= 500)) {
            lastColorToggle = now;
            dayNightColorMode = !dayNightColorMode;
            playColorToggle(dayNightColorMode);
            
            // Redraw entire board with correct colors
            display.fillScreen(COLOR_BLACK);
            if (showGridLines) {
                drawFullGrid();
            }
            for (int x = 0; x < BOARD_WIDTH; x++) {
                for (int y = 0; y < BOARD_HEIGHT; y++) {
                    if (board[x][y]) {
                        if (dayNightColorMode) {
                            drawCell(x, y, getDayNightColorByAge(cellAges[x][y], true));
                        } else {
                            drawCell(x, y, COLOR_WHITE);
                        }
                    }
                }
            }
    
            return;  // Don't process other buttons this frame
        }
    }

    // CHECK FOR BRIAN'S BRAIN COLOR MODE TOGGLE (UP + DOWN together)
    if (!inMenuMode && !editMode && briansBrainMode) {
        if (isBtnUp() && isBtnDown() && (now - lastColorToggle >= 500)) {
            lastColorToggle = now;
            briansBrainColorMode = !briansBrainColorMode;
            playColorToggle(briansBrainColorMode);
            
            // Redraw entire board
            display.fillScreen(COLOR_BLACK);
            if (showGridLines) {
                drawFullGrid();
            }
            drawBriansBrain();
            
            return;  // Don't process other buttons this frame
        }
    }
    
    // Menu mode handling
    if (inMenuMode) {
        if (buttonProcessingInProgress) {
            if (!isBtnSet()) {
                buttonProcessingInProgress = false;
            }
            return;
        }
        
        // UP button in menu
        if (isBtnUp()) {
            if (now - lastUpPress >= 150) {
                lastUpPress = now;
                playMenuBeep();
                
                int oldSel, newSel;
                
                 if (inToolsSubMenu) {
                    oldSel = toolsSelection;
                    toolsSelection = (toolsSelection > 1) ? toolsSelection - 1 : 6;
                    newSel = toolsSelection;
                    updateMenuSelection(oldSel, newSel);
                }else if (inSymmetricSubMenu) {
                    oldSel = symmetricSizeSelection;
                    symmetricSizeSelection = (symmetricSizeSelection > 1) ? symmetricSizeSelection - 1 : 3;
                    newSel = symmetricSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inAltGamesSubMenu) {
                    oldSel = altGameSelection;
                    altGameSelection = (altGameSelection > 1) ? altGameSelection - 1 : 4;
                    newSel = altGameSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inBriansBrainSubMenu) {
                    oldSel = briansBrainSizeSelection;
                    briansBrainSizeSelection = (briansBrainSizeSelection > 1) ? briansBrainSizeSelection - 1 : 5;
                    newSel = briansBrainSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inCyclicSubMenu) {
                    oldSel = cyclicSizeSelection;
                    cyclicSizeSelection = (cyclicSizeSelection > 1) ? cyclicSizeSelection - 1 : 3;
                    newSel = cyclicSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inDayNightSubMenu) {
                    oldSel = dayNightDensitySelection;
                    dayNightDensitySelection = (dayNightDensitySelection > 1) ? dayNightDensitySelection - 1 : 2;
                    newSel = dayNightDensitySelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inSeedsSubMenu) {
                    oldSel = seedsGameSelection;
                    seedsGameSelection = (seedsGameSelection > 1) ? seedsGameSelection - 1 : 2;
                    newSel = seedsGameSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inCellSizeSubMenu) {
                    oldSel = cellSizeSelection;
                    cellSizeSelection = (cellSizeSelection > 1) ? cellSizeSelection - 1 : 4;
                    newSel = cellSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inPatternsSubMenu) {
                    oldSel = patternSelection;
                    patternSelection = (patternSelection > 1) ? patternSelection - 1 : 5;
                    newSel = patternSelection;
                    updateMenuSelection(oldSel, newSel);
                } else {
                    oldSel = menuSelection;
                    menuSelection = (menuSelection > 1) ? menuSelection - 1 : 7;
                    newSel = menuSelection;
                    updateMenuSelection(oldSel, newSel);
                }
            }
        }
        
        // DOWN button in menu  
        if (isBtnDown()) {
            if (now - lastDownPress >= 150) {
                lastDownPress = now;
                playMenuBeep();  // Flash FIRST to confirm button detected
                
                int oldSel, newSel;
                
                if (inToolsSubMenu) {
                    oldSel = toolsSelection;
                    toolsSelection = (toolsSelection < 6) ? toolsSelection + 1 : 1;
                    newSel = toolsSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inSymmetricSubMenu) {
                    oldSel = symmetricSizeSelection;
                    symmetricSizeSelection = (symmetricSizeSelection < 3) ? symmetricSizeSelection + 1 : 1;
                    newSel = symmetricSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inAltGamesSubMenu) {
                    oldSel = altGameSelection;
                    altGameSelection = (altGameSelection < 4) ? altGameSelection + 1 : 1;
                    newSel = altGameSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inBriansBrainSubMenu) {
                    oldSel = briansBrainSizeSelection;
                    briansBrainSizeSelection = (briansBrainSizeSelection < 5) ? briansBrainSizeSelection + 1 : 1;
                    newSel = briansBrainSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inCyclicSubMenu) {
                    oldSel = cyclicSizeSelection;
                    cyclicSizeSelection = (cyclicSizeSelection < 3) ? cyclicSizeSelection + 1 : 1;
                    newSel = cyclicSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inDayNightSubMenu) {
                    oldSel = dayNightDensitySelection;
                    dayNightDensitySelection = (dayNightDensitySelection < 2) ? dayNightDensitySelection + 1 : 1;
                    newSel = dayNightDensitySelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inSeedsSubMenu) {
                    oldSel = seedsGameSelection;
                    seedsGameSelection = (seedsGameSelection < 2) ? seedsGameSelection + 1 : 1;
                    newSel = seedsGameSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inCellSizeSubMenu) {
                    oldSel = cellSizeSelection;
                    cellSizeSelection = (cellSizeSelection < 4) ? cellSizeSelection + 1 : 1;
                    newSel = cellSizeSelection;
                    updateMenuSelection(oldSel, newSel);
                } else if (inPatternsSubMenu) {
                    oldSel = patternSelection;
                    patternSelection = (patternSelection < 5) ? patternSelection + 1 : 1;
                    newSel = patternSelection;
                    updateMenuSelection(oldSel, newSel);
                } else {
                    oldSel = menuSelection;
                    menuSelection = (menuSelection < 7) ? menuSelection + 1 : 1;
                    newSel = menuSelection;
                    updateMenuSelection(oldSel, newSel);
                }
            }
        }
        
        return;
    }
    
    // Edit mode handling - IMPROVED with proper button state tracking
    if (editMode) {
        static bool lastUpState = false;
        static bool lastDownState = false;
        static bool lastLeftState = false;
        static bool lastRightState = false;
        
        bool upPressed = isBtnUp();
        bool downPressed = isBtnDown();
        bool leftPressed = isBtnLeft();
        bool rightPressed = isBtnRight();
        
        // UP - trigger only on button press (not hold)
        if (upPressed && !lastUpState && (now - lastUpPress >= 150)) {
            lastUpPress = now;
            cursorY--;
            if (cursorY < 0) cursorY = BOARD_HEIGHT - 1;
            // NO SOUND - removed flashLED/playMenuBeep
        }
        lastUpState = upPressed;
        
        // DOWN - trigger only on button press (not hold)
        if (downPressed && !lastDownState && (now - lastDownPress >= 150)) {
            lastDownPress = now;
            cursorY++;
            if (cursorY >= BOARD_HEIGHT) cursorY = 0;
            // NO SOUND - removed flashLED/playMenuBeep
        }
        lastDownState = downPressed;
        
        // LEFT - trigger only on button press (not hold)
        if (leftPressed && !lastLeftState && (now - lastLeftPress >= 150)) {
            lastLeftPress = now;
            cursorX--;
            if (cursorX < 0) cursorX = BOARD_WIDTH - 1;
            // NO SOUND - removed flashLED/playMenuBeep
        }
        lastLeftState = leftPressed;
        
        // RIGHT - trigger only on button press (not hold)
        if (rightPressed && !lastRightState && (now - lastRightPress >= 150)) {
            lastRightPress = now;
            cursorX++;
            if (cursorX >= BOARD_WIDTH) cursorX = 0;
            // NO SOUND - removed flashLED/playMenuBeep
        }
        lastRightState = rightPressed;
        
        return;
    }
    
    // Game mode handling (not in edit mode)
    
    // UP = Increase speed (but not if DOWN is also pressed for color toggle)
if (isBtnUp() && !isBtnDown() && (now - lastUpPress >= 150)) {
    lastUpPress = now;
    if (currentSpeedIndex < numSpeeds - 1) {
        currentSpeedIndex++;
        frame_start_t = now;
        playSpeedChange(currentSpeedIndex);
    }
}

// DOWN = Decrease speed (but not if UP is also pressed for color toggle)
if (isBtnDown() && !isBtnUp() && (now - lastDownPress >= 150)) {
    lastDownPress = now;
    if (currentSpeedIndex > 0) {
        currentSpeedIndex--;
        frame_start_t = now;
        playSpeedChange(currentSpeedIndex); 
    }
}
    
    // LEFT = Change cell size (but not if RIGHT is also pressed for color toggle)
    if (isBtnLeft() && !isBtnRight() && (now - lastLeftPress >= 250)) {
        lastLeftPress = now;
        changeCellSize();
    }
    
    // RIGHT = Reset/Regenerate (but not if LEFT is also pressed for color toggle)
    if (isBtnRight() && !isBtnLeft() && (now - lastRightPress >= 250)) {
        lastRightPress = now;
        
        // Clear screen first for all resets
        display.fillScreen(COLOR_BLACK);
        
        if (gameMode == 1 || ruleExplorerMode) {
            showingStats = false;
            randomizeBoard();
            if (showGridLines) {
                drawFullGrid();
            }
            generation = 0;
            cellCount = 0;
            max_cellCount = 0;
            start_t = micros();
            frame_start_t = millis();
            playReset();
        } else if (dayNightMode) {
            showingStats = false;
            initializeDayNight();
            if (showGridLines) {
                drawFullGrid();
            }
            generation = 0;
            cellCount = 0;
            max_cellCount = 0;
            start_t = micros();
            frame_start_t = millis();
            playReset();
        } else if (gameMode == 9 && cyclicMode) {
            showingStats = false;
            initializeCyclic();
            if (showGridLines) {
                drawFullGrid();
            }
            generation = 0;
            cellCount = 0;
            start_t = micros();
            frame_start_t = millis();
            playReset();
        } else if (gameMode == 6 && seedsMode) {
            showingStats = false;
            if (seedsWasCustom) {
                clearBoard();
                if (showGridLines) {
                    drawFullGrid();
                }
                editMode = true;
                cursorX = BOARD_WIDTH / 2;
                cursorY = BOARD_HEIGHT / 2;
            } else {
                initializeSeeds();
                if (showGridLines) {
                    drawFullGrid();
                }
            }
            generation = 0;
            cellCount = 0;
            start_t = micros();
            frame_start_t = millis();
            playReset();
        } else if (gameMode == 4) {
            showingStats = false;
            clearBoard();
            currentSymmetryType = random(3) + 1;
            
            switch(currentSymmetrySize) {
                case 1: generateSmallSymmetricPattern(currentSymmetryType); break;
                case 2: generateMediumSymmetricPattern(currentSymmetryType); break;
                case 3: generateSymmetricPattern(currentSymmetryType); break;
            }
            if (showGridLines) {
                drawFullGrid();
            }
            generation = 0;
            cellCount = 0;
            max_cellCount = 0;
            start_t = micros();
            frame_start_t = millis();
            playReset();
        } else if (gameMode == 8 && briansBrainMode) {
            showingStats = false;
            clearBoard();
            
            if (briansBrainWasCustom) {
                if (showGridLines) {
                    drawFullGrid();
                }
                editMode = true;
                cursorX = BOARD_WIDTH / 2;
                cursorY = BOARD_HEIGHT / 2;
                generation = 0;
            } else {
                switch(currentBriansBrainSize) {
                    case 1: generateBriansBrainSmall(); break;
                    case 2: generateBriansBrainMedium(); break;
                    case 3: generateBriansBrainLarge(); break;
                    case 4: generateBriansBrainRandom(); break;
                }
                if (showGridLines) {
                    drawFullGrid();
                }
                generation = 0;
                cellCount = 0;
                max_cellCount = 0;
                briansBrain_lowCellCounter = 0;
                frame_start_t = millis();
                start_t = micros();
                drawBriansBrain();
            }
            playReset();
        }
    }
}