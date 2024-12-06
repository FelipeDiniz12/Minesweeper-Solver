/**
 * This is a Minesweeper Solver C++ code. Much of its numbers and metrics used are
 * purely empiric. So, it might need some tweaks in case you want to try it yourself.
 * Besides it, other two libraries/frameworks were used in this project:
 * * OpenCV
 * * X11
 */ 

#include <vector>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <cstdint>
#include <cstring>
#include <unistd.h>

// Namespaces
using namespace cv;

// Enums
// Colors
enum COLOR {
    LIGHT_GRAY=0,
    BLUE=1,
    GREEN=2,
    RED=3,
    DARK_BLUE=4,
    BROWN=5,
    LIGHT_GREEN=6,
    BLACK=7,
    GRAY=8,
    UNKNOWN=9,
    WHITE=10
};
// Actions during working with bombs and freeing tiles
enum ACTION {
    REVEAL_TILE=Button1,
    MARK_BOMB=Button3
};
// Which strategy to use before choosing an action
enum STRATEGY {
    SIMPLE=0,
    PIVOT=1
};
// Board difficulty
enum DIFFICULTY {
    BEGINNER=0,
    INTERMEDIATE=1,
    EXPERT=2
};

/**
 * This function prints the current board to the screen
 * @param board Board to be used for printing
 */
void printBoard(std::vector<std::vector<char>>& board) {
    for (auto i : board) {
        for (auto j : i) {
            std::cout << j << " ";
        }
        std::cout << std::endl;
    }
}

/**
 * This function gets the screenshot from a given display.
 * @param Pixels pixel vector to be stored
 * @param Width width from the image to be stored
 * @param Height height from the image to be stored
 * @param BitsPerPixel bits per pixel to be used
 */
void ImageFromDisplay(std::vector<uint8_t>& Pixels, int& Width, int& Height, int& BitsPerPixel)
{
    // Setting is as nullptr means that the env var DISPLAY value will be used (likely to be ":0")
    Display* display = XOpenDisplay(nullptr);
    Window root = DefaultRootWindow(display);

    XWindowAttributes attributes = {0};
    XGetWindowAttributes(display, root, &attributes);

    Width = attributes.width;
    Height = attributes.height;

    XImage* img = XGetImage(display, root, 0, 0 , Width, Height, AllPlanes, ZPixmap);
    BitsPerPixel = img->bits_per_pixel;
    Pixels.resize(Width * Height * 4);

    memcpy(&Pixels[0], img->data, Pixels.size());

    XDestroyImage(img);
    XCloseDisplay(display);
}

/**
 * This function mocks a mouse click given a button and a mask.
 * @param button Which mouse button to be used
 * @param mask Which mask to be used to avoid unintended clicks/commands
 */ 
void mouseClick(int button, int mask) {
    Display *display = XOpenDisplay(NULL);

    XEvent event;

    if(display == NULL)
    {
        fprintf(stderr, "Errore nell'apertura del Display !!!\n");
        exit(EXIT_FAILURE);
    }

    memset(&event, 0x00, sizeof(event));

    event.type = ButtonPress;
    event.xbutton.button = button;
    event.xbutton.same_screen = True;
    event.xbutton.time = 100;

    XQueryPointer(display, RootWindow(display, DefaultScreen(display)), &event.xbutton.root, &event.xbutton.window, &event.xbutton.x_root, &event.xbutton.y_root, &event.xbutton.x, &event.xbutton.y, &event.xbutton.state);

    event.xbutton.subwindow = event.xbutton.window;

    while(event.xbutton.subwindow)
    {
        event.xbutton.window = event.xbutton.subwindow;

        XQueryPointer(display, event.xbutton.window, &event.xbutton.root, &event.xbutton.subwindow, &event.xbutton.x_root, &event.xbutton.y_root, &event.xbutton.x, &event.xbutton.y, &event.xbutton.state);
    }

    if(XSendEvent(display, PointerWindow, True, mask, &event) == 0) fprintf(stderr, "Error\n");

    XFlush(display);

    usleep(100000);

    event.type = ButtonRelease;
    event.xbutton.state = mask;

    if(XSendEvent(display, PointerWindow, True, mask, &event) == 0) fprintf(stderr, "Error\n");

    XFlush(display);

    XCloseDisplay(display);
}

/**
 * This function returns a color based on a Vec4b provided. The numbers for R, G, B thresholds are empiric.
 * @param color Vec4b containing RGBA information to be processed
 * @return returns a COLOR enum with the color identified by this function. If no color is found, then it returns UNKNOWN
 */ 
COLOR colorIdentifier (Vec4b color) {
    int R = color.val[2];
    int G = color.val[1];
    int B = color.val[0];
    
    // White world
    if (R > 195 && G > 195 && B > 195) return WHITE;
    // Red world
    if (R > 180 && G < 95 && B < 95) return RED;
    else if (R > 105 && G < 60 && B < 60) return BROWN;
    // Green world
    if (R < 95 && G > 105 && B < 95) return GREEN;
    else if (R < 60 && G > 105 && B > 110) return LIGHT_GREEN;
    // Blue world
    if (R < 95 && G < 95 && B > 180) return BLUE;
    else if (R < 95 && G < 95 && B > 105) return DARK_BLUE;
    // Extreme colors world
    if (R < 150 && G < 150 && B < 150 && R==G && R==B && G==B) return LIGHT_GRAY;
    else if (R > 150 && G > 150 && B > 150 && R==G && R==B && G==B) return GRAY;
    else if (R < 50 && G < 50 && B < 50) return BLACK;
    
    return UNKNOWN;
}

/**
 * This function returns an average RGBA vector from a given pixel and its neighbor-pixels
 * @param img Image to be used as reference
 * @param x X position to be used as center
 * @param y Y position to be used as center
 * @param offsetDepth This is how depth the average will be calculated. By default is 3. Which means, the average
 * will take into account the pixels at maximum 3 pixels of distance on any direction.
 * @return returns a Vec4b vector containing the average RGBA.
 */ 
Vec4b pixelAverage(Mat img, int x, int y, int offsetDepth = 3) {
    Vec4b average_vec = {0, 0, 0, 0};
    float denominator = 1/(std::pow(2*offsetDepth+1, 2));
    for (int j = -offsetDepth; j < offsetDepth+1; j++) {
        for (int k = -offsetDepth; k < offsetDepth+1; k++) {
            for (int i = 0; i < 4; i++) {
                Vec4b color = img.at<Vec4b>(y+k,x+j);
                average_vec.val[i] += color.val[i]*denominator;
            }
        }
    }
    return average_vec;
}

/**
 * This function returns a vector containing the surrouding tiles from a given position
 * that matches with the list provided.
 * @param board Board to be used
 * @param x X position for search the surrounding tiles
 * @param y Y position for search the surrounding tiles
 * @param searchList list of chars to be used as "valid tiles". The list is "E" and "M" by default.
 * @return returns the vector containing the surrounding tiles that matches with searchList contents
 */ 
std::vector<std::vector<int>> surroundingTiles(std::vector<std::vector<char>>& board, int x, int y, std::vector<char> searchList = {'E', 'M'}) {
    std::vector<std::vector<int>> result = {};
    // Iterate through the surroundings from x,y position
    for (int i = 0; i < 9; i++) {
        int pos_x = x-1+i/3 < 0 ? 0 : x-1+i/3 >= board.size() ? board.size()-1 : x-1+i/3;
        int pos_y = y-1+i%3 < 0 ? 0 : y-1+i%3 >= board[0].size() ? board[0].size()-1 : y-1+i%3;
        if (std::find(searchList.begin(), searchList.end(), board.at(pos_x).at(pos_y)) != searchList.end()) {
            std::vector<int> aux = {pos_x, pos_y};
            if (std::find(result.begin(), result.end(), aux) == result.end()) result.push_back({pos_x, pos_y});
        }
    }
    return result;
}

/**
 * This function updates the board and at the end, prints it out.
 * @param board Board to be updated
 * @return returns true when the function finishes
 */ 
bool updateBoard(std::vector<std::vector<char>>& board) {
    int i = 0;
    int j = 0;
    int Width = 0;
    int Height = 0;
    int Bpp = 0;
    std::vector<std::uint8_t> Pixels;
    // Give the original game time to update the tiles accordingly
    usleep(80000);

    // Collect the new image from the board
    ImageFromDisplay(Pixels, Width, Height, Bpp);
    Mat img = Mat(Height, Width, Bpp > 24 ? CV_8UC4 : CV_8UC3, &Pixels[0]);

    // That's kind of the same approach from the original board population in main() function
    // TODO: Used this function in main()

    // This corrector is needed for handling pixel issues when iterating through the rows.
    // TODO: very likely the correction is needed on Y-axis
    int corrector_x = 0;
    for (int offset_row=318; offset_row < 530+(board.size()-9)*25; offset_row += 25) {
        j = 0;
        int tile_counter = 0;
        corrector_x = 0;
        Vec4b real_color = {0, 0, 0, 0};
        int counter = 0;
        for (int offset=34; offset<260+(board[0].size()-9)*25; offset += 1) {
            if (board.at(i).at(j) != 'E' || board.at(i).at(j) == '0' || board.at(i).at(j) == 'M' || board.at(i).at(j) == '9') {
                std::cout << "Position kept as " << board.at(i).at(j) << " at " << i+1 << " " << j+1 << std::endl;
                j += 1;
                offset = 34+25*j;
                tile_counter = 0;
                counter = 0;
                real_color = {0, 0, 0, 0};
                continue;
            }
            corrector_x = j/2;

            int x = offset + corrector_x;
            int y = offset_row;

            Vec4b current_color = img.at<Vec4b>(y,x);
            
            if (tile_counter == 25 || ((260+(board[0].size()-9)*25)) - offset <= 1) {
                tile_counter = 0;
                COLOR color_verdict;
                if (counter > 0) {
                    color_verdict = colorIdentifier(real_color);
                    printf("NEW Position: %i %i at x:%i y:%i : %i, %i, %i, %i, VERDICT: %i\n", i+1, j+1, x, y, real_color.val[3],real_color.val[2],real_color.val[1], real_color.val[0], color_verdict);
                } else {
                    color_verdict = LIGHT_GRAY;
                    printf("NEW Position: %i %i at x:%i y:%i :FORCED LIGHT GRAY, VERDICT: %i\n", i+1, j+1, x, y, color_verdict);
                }
                
                real_color = {0, 0, 0, 0};
                counter = 0;
                if (color_verdict) {
                    board.at(i).at(j) = (char)(48+color_verdict);
                } else {
                    // Need to know if tile was cliked, or not... The distinguishment will be done based on
                    // the information that an unclicked-tile has a white pixel range on its border.
                    
                    // Begin at the tile's border
                    int old_x = x;
                    x = 34+25*j + corrector_x;
                    while(x <= 34+25*(j+1)) {
                        // Redo the average routine and color identification
                        Vec4b color_intermediate = pixelAverage(img, x, y, 0);
                        color_verdict = colorIdentifier(color_intermediate);
                        printf("INTERMEDIATE Position: %i %i at x:%i y:%i : %i, %i, %i, %i, VERDICT: %i\n", i+1, j+1, x, y, color_intermediate.val[3],color_intermediate.val[2],color_intermediate.val[1], color_intermediate.val[0], color_verdict);

                        if (color_verdict == WHITE) {
                            board.at(i).at(j) = 'E';
                            x = old_x;
                            break;
                        } else if (34+25*(j+1)-x < 1) {
                            board.at(i).at(j) = '0';
                            x = old_x;
                            break;
                        }
                        x++;
                    }
                    x = old_x;
                }
                j++;
            } else {
                if (current_color.val[2] > 110 && current_color.val[1] > 110 && current_color.val[0] > 110) {
                    tile_counter++;
                    continue;
                } else {
                    counter++;
                    for (int aux = 0; aux < 4; aux++) {
                        real_color.val[aux] = (real_color.val[aux]*(counter-1) + current_color.val[aux])/(counter);
                    }
                }
            }
            tile_counter++;
        }
        i++;
    }
    
    // Prints the board at the very end.
    printBoard(board);
    std::cout << std::endl << std::endl ;
    return true;
}

/**
 * This function warps the mouse cursor to a given position, and performs a click action. After this, it
 * updates the board with the new positions discovered.
 * @param board Board to be updated
 * @param x X position to warp the cursor
 * @param y Y position to warp the cursor
 * @param action Action to take on click, either Left (REVEAL_TILE) or Right (MARK_BOMB)
 * @param delay Create a delay before and after the click for debug purposes. It's false by default
 * @return returns true at the end of the function.
 */
bool warpAndClick(std::vector<std::vector<char>>& board, int x, int y, ACTION action, bool delay = false) {
    Display* display = XOpenDisplay(nullptr);
    Window root = DefaultRootWindow(display);
    // This correction is needed to overcome issues with pixel count and warpings.
    // Totally empiric
    int j = (x-46)/25;
    int corrector = 4*j/9;
    // TODO: Instead of using x and y already in pixel position, let's do the conversion here.
    XWarpPointer (display, None, root, 0,0,0,0, x+corrector, y);
    XFlush(display);
    XCloseDisplay(display);
    // This mask is used to ensure the left/right clicks are done without influence from other
    // clicks.
    int mask = action == Button1 ? 0x001 : 0x002;
    if (delay) sleep(1);
    mouseClick(action, mask);
    if (delay) sleep(1);
    // TODO: We only need to update the board when revealing tiles, right?
    updateBoard(board);
    return true;
}

/**
 * This function checks if a vector is contained in another one
 * @param major The bigger vector
 * @param minor The smaller vector to check if exists inside the major one
 * @return returns false if the vector isn't inside the major one, otherwise, returns true
 */
bool vectorInside(std::vector<std::vector<int>> major, std::vector<std::vector<int>> minor) {
    for (auto in : minor) {
        if (std::find(major.begin(), major.end(), in) == major.end()) return false;
    }
    return true;
}

/**
 * This function mark tiles with bombs, or free them
 * @param board Board with the tiles freed, not-freed and bombs marked
 * @param x X coordinate from original tile
 * @param y Y coordinate from original tile
 * @param pivot_x X coordinate from pivot tile
 * @param pivot_y Y coordinate from pivot tile
 * @param surroundings List of surrounding tiles (E and M) from original tile
 * @param results List of surrounding tiles (E) from original tile
 * @param bomb_counter Amount of bombs in the surrounding of the original tile
 * @return returns true if a modification was done in the board, false if not
 */
bool pivotBoard(std::vector<std::vector<char>>& board, int x, int y, int pivot_x, int pivot_y, std::vector<std::vector<int>> surroundings, std::vector<std::vector<int>> results, int bomb_counter) {
    // TODO: does this function needs so many arguments?
    int bombs = board.at(x).at(y)-48;
    char pivot = board.at(pivot_x).at(pivot_y);
    if (pivot != 'E' && pivot != '0' && pivot != 'M') {
        std::cout << "Valid pivoting at " << x+1 << " " << y+1 << std::endl;
        std::cout << "Pivot position is: " << pivot_x+1 << " " << pivot_y+1 << std::endl;
        int pivot_bombs = board.at(pivot_x).at(pivot_y)-48;
        int pivot_bomb_counter = 0;
        std::vector<std::vector<int>> pivot_results;
        std::vector<std::vector<int>> pivot_surroundings = surroundingTiles(board, pivot_x, pivot_y);
        
        // Separate marked bombs from bomb-candidates for pivot
        if (pivot_surroundings.size() > 0) {
            std::cout << "Pivot surroundings results are: ";
            for (auto &i : surroundingTiles(board, pivot_x, pivot_y)) {
                if (board.at(i[0]).at(i[1]) != 'M') {
                    pivot_results.emplace_back(i);
                    std::cout << i[0]+1 << " " << i[1]+1 << " ";
                } else {
                    std::cout << "\nIncreasing pivot bomb counter due to position " << i[0]+1 << " " << i[1]+1 << std::endl;
                    std::cout << "At this position, it was found: " << board.at(i[0]).at(i[1]) << std::endl;
                    pivot_bomb_counter++;
                }
            }
        }
        std::cout << std::endl;

        int pivot_expected_bombs = pivot_bombs - pivot_bomb_counter;
        std::cout << "Bomb count for pivot is " << pivot_bomb_counter << std::endl;
        std::cout << pivot_expected_bombs << " bombs are expected in these surroundings for pivot" << std::endl;

        // Get tile's intersection:
        std::vector<std::vector<int>> results_intersection;
        std::vector<std::vector<int>> results_not_intersection;
        for (std::vector<std::vector<int>>::iterator i = results.begin(); i != results.end(); i++) {
            if (std::find(pivot_results.begin(), pivot_results.end(), *i) != pivot_results.end()) {
                std::cout << "Intersection found at " << i->at(0)+1 << " " << i->at(1)+1 << std::endl;
                results_intersection.emplace_back(*i);
            } else {
                if (board.at(i->at(0)).at(i->at(1)) != 'M') {
                    std::cout << "Intersection NOT found at " << i->at(0)+1 << " " << i->at(1)+1 << std::endl;
                    results_not_intersection.emplace_back(*i);
                }
            }
        }

        // Get positions from pivot which weren't intersected
        std::vector<std::vector<int>> pivot_not_intersection;
        for (std::vector<std::vector<int>>::iterator i = pivot_surroundings.begin(); i != pivot_surroundings.end(); i++) {
            if (std::find(results_intersection.begin(), results_intersection.end(), *i) == results_intersection.end()) {
                if (board.at(i->at(0)).at(i->at(1)) != 'M') {
                    std::cout << "After reading the pivot surroundings, adding " << i->at(0)+1 << " " << i->at(1)+1 << " to NOT INTERSECTION" << std::endl;
                    results_not_intersection.emplace_back(*i);
                    pivot_not_intersection.emplace_back(*i);
                }
            }
        }

        int expected_bombs = bombs - bomb_counter;

        std::cout << "Expected bombs in original tile is " << expected_bombs << std::endl;
        std::cout << "Original tile surroundings are:";
        for (auto &i : surroundings) {
            std::cout << " " << i[0]+1 << " " << i[1]+1 ; 
        }
        std::cout << std::endl;

        // If the pivot expected bombs are bigger than the expected bombs in original tile, and if
        // the non-intersected tiles list from pivot aren't empty, we can mark bombs from this list.
        // For example, let's say you are on row 2 and column 2 (1) and pivoting to the right (2):
        /*
            0 0 0 0
            2 1 2 1
            E E E E
        */
       // The pivot expects 2 bombs, while the original only 1. The difference of expected bombs is 1
       // Which is also the size of the list of tiles non-intersected from the pivot (row 3 column 4).
       // So, this is a tile that is for sure a bomb. This can be scaled to multiple bombs, so we mark
       // all tiles from this list as bombs.
        if (pivot_expected_bombs > expected_bombs) {
            if (pivot_not_intersection.size() == 0) return false;
            std::cout << "More pivot expected bombs than original expected bombs..." << std::endl;
            std::cout << "Pivot expected: " << pivot_expected_bombs << std::endl;
            std::cout << "Original expected: " << expected_bombs << std::endl;
            int difference = pivot_expected_bombs - expected_bombs;
            if (difference == pivot_not_intersection.size()) {
                std::cout << "Pivoting taking place!" << std::endl;
                std::cout << "Since this invalidates the original tile, then marking the other pivot tiles as bombs!" << std::endl;
                for (auto &i : pivot_not_intersection) {
                    std::cout << "Marking bomb at " << i[0]+1 << " " << i[1]+1 << std::endl;
                    board.at(i[0]).at(i[1]) = 'M';
                    warpAndClick(board, 46+25*(i[1]), 318+25*(i[0]), MARK_BOMB, false);
                }
                return true;
            }
        } 

        // TODO: are these sortings really needed?
        std::sort(results.begin(), results.end());
        std::sort(results_intersection.begin(), results_intersection.end());
        
        
        // If the amount of expected bombs from pivot and original are the same, so as the pivot surroundings
        // and the intersection between pivot and original tile, it means all bombs reside in the intersection
        // list. Therefore, the other tiles from the original tile can be freed.
        // For example, let's say you are on row 2 and column 3 (3) and pivoting to the left (2):
        /*
            0 0 2 E
            1 2 3 M
            1 E E E
        */
       // The expected bombs for pivot and original is 2 (Note that there's already a bomb marked in row 2 column 4)
       // Also, the pivot surroundings (tiles with E) and the intersection with the original tile are the same.
       // (Positions 3,2 and 3,3). So, the other tiles (Positions 1,4 and 3,4) can be freed.
        if (pivot_expected_bombs == expected_bombs && std::equal(pivot_surroundings.begin(), pivot_surroundings.end(), results_intersection.begin(), results_intersection.end())) {
            if (results_not_intersection.size() == 0) return false;
            std::cout << "Pivoting taking place!" << std::endl;
            std::cout << "Surroundings from original tile are the same from the results intersection" << std::endl;
            std::cout << "We can free all other tiles not in the intersection!" << std::endl;
            for (auto &i : results_not_intersection) {
                std::cout << "Revealing tile " << i[0]+1 << " " << i[1]+1 << std::endl;
                warpAndClick(board, 46+25*(i[1]), 318+25*(i[0]), REVEAL_TILE, false);
            }
            return true;
        }

        // This is the same scenario as above, but with the focus on the pivot. Note that we are comparing different lists
        // and finally freeing the non-intersecting tiles from the pivot.
        if (pivot_expected_bombs == expected_bombs && std::equal(results.begin(), results.end(), results_intersection.begin(), results_intersection.end())) {
            if (pivot_not_intersection.size() == 0) return false;
            std::cout << "Pivoting taking place!" << std::endl;
            std::cout << "Surroundings from original tile are the same from the results intersection" << std::endl;
            std::cout << "We can free all other tiles not in the intersection!" << std::endl;
            for (auto &i : pivot_not_intersection) {
                std::cout << "Revealing tile " << i[0]+1 << " " << i[1]+1 << std::endl;
                warpAndClick(board, 46+25*(i[1]), 318+25*(i[0]), REVEAL_TILE, false);
            }
            return true;
        }

        // In case the amount of expected bombs from pivot and original are the same, so as the original tile
        // has no non-intersecting tiles, this means all the other non-intersecting tiles from pivot can be freed.
        // TODO: is this really a pivot scenario?
        if (pivot_expected_bombs == expected_bombs && results_not_intersection.size() == 0) {
            if (pivot_not_intersection.size() == 0) return false;
            std::cout << "Pivoting taking place!" << std::endl;
            std::cout << "The results not intersection size is 0, then let's reveal the pivots not intersections" << std::endl;
            for (auto &i : pivot_not_intersection) {
                std::cout << "Revealing tile " << i[0]+1 << " " << i[1]+1 << std::endl;
                warpAndClick(board, 46+25*(i[1]), 318+25*(i[0]), REVEAL_TILE, false);
            }
            return true;
        }

        // If the difference of expected bombs from original tile and pivot is equal to the size of the list 
        // with non-intersected tiles from original one, and this list isn't empty, then all these tiles should
        // be marked as bombs.
        if (expected_bombs - pivot_expected_bombs == results_not_intersection.size()) {
            if (results_not_intersection.size() == 0) return false;
            std::cout << "Pivoting taking place!" << std::endl;
            std::cout << "The NOT intersection size is the same amount of expected bombs difference, marking as bomb!" << std::endl;
            for (auto &i : results_not_intersection) {
                std::cout << "Marking bomb at " << i[0]+1 << " " << i[1]+1 << std::endl;
                board.at(i[0]).at(i[1]) = 'M';
                warpAndClick(board, 46+25*(i[1]), 318+25*(i[0]), MARK_BOMB, false);
            }
            return true;
        }
    }
    // None strategy was successfull. Return false.
    return false;
}

/**
 * This function mark tiles with bombs, or free them
 * @param board Board with the tiles freed, not-freed and bombs marked
 * @param x X coordinate of the board
 * @param y Y coordinate of the board
 * @param strategy Strategy chosen for mark bombs or free tiles. The default one is SIMPLE.
 * @return returns true if a modification was done in the board, false if not
 */
bool markBombs(std::vector<std::vector<char>>& board, int x, int y, STRATEGY strategy = SIMPLE) {
    // Converting the amount of bombs from char to int (Since this comes from ASCII table, just
    // subtracting it by 48)
    int bombs = board.at(x).at(y)-48;
    // bomb_counter will track how much bombs exist in the tile's neighborhood, and the possible locations
    // will be stored in results vector.
    int bomb_counter = 0;
    std::vector<std::vector<int>> results;
    std::vector<std::vector<int>> surroundings = surroundingTiles(board, x, y);
    // Separate marked bombs from bomb-candidates
    if (surroundings.size() > 0) {
        for (auto &i : surroundingTiles(board, x, y)) {
            if (board.at(i[0]).at(i[1]) != 'M') {
                results.emplace_back(i);
            } else {
                bomb_counter++;
            }
        }
    }

    // Based on the strategy, mark bombs and/or free tiles.
    if (strategy == SIMPLE) {
        std::cout << "Found a " << bombs << " tile in position " << x+1 << " " << y+1 << std::endl;
        // If bomb counter is the amount of bombs, then we already know the positions!
        if (bomb_counter == bombs ) {
            std::cout << "Bomb counter is " << bombs << " in position " << x+1 << " " << y+1 << std::endl;
            std::cout << "Revealing tile at " << x+1 << " " << y+1 << std::endl;
            warpAndClick(board, 46+25*(y), 318+25*(x), REVEAL_TILE);
            return true;
        } else if (bomb_counter + results.size() == bombs) {
            std::cout << "Bomb counter summed with results size is " << bombs << " in position " << x+1 << " " << y+1 << std::endl;
            for (auto i : results) {
                std::cout << "Marking bomb at " << i[0]+1 << " " << i[1]+1 << std::endl;
                board.at(i[0]).at(i[1]) = 'M';
                warpAndClick(board, 46+25*(i[1]), 318+25*(i[0]), MARK_BOMB);
            }
            return true;
        } 
    } else if(strategy == PIVOT) {
        // Pivoting...
        std::cout << "Valid pivot case! Trying pivoting at " << x+1 << " " << y+1 << std::endl;
        int pivot_x;
        int pivot_y;
        // There are 4 possible pivotings, left, right, up and down. However, we need to check
        // if the pivoting is possible, and valid.
        if (y > 0) {
            // left
            std::cout << "Pivoting to the left" << std::endl;
            pivot_x = x;
            pivot_y = y-1;
            if (pivotBoard(board, x, y, pivot_x, pivot_y, surroundings, results, bomb_counter)) return true;
        } 
        
        if (y < board[0].size()-1) {
            // right
            std::cout << "Pivoting to the right" << std::endl;
            pivot_x = x;
            pivot_y = y+1;
            if (pivotBoard(board, x, y, pivot_x, pivot_y, surroundings, results, bomb_counter)) return true;
        }
        
        if(x > 0) {
            // up
            std::cout << "Pivoting up" << std::endl;
            pivot_x = x-1;
            pivot_y = y;
            if (pivotBoard(board, x, y, pivot_x, pivot_y, surroundings, results, bomb_counter)) return true;
        } 

        if(x < board.size()-1) {
            // down
            std::cout << "Pivoting down" << std::endl;
            pivot_x = x+1;
            pivot_y = y;
            if (pivotBoard(board, x, y, pivot_x, pivot_y, surroundings, results, bomb_counter)) return true;
        } else {
            // That's a fallback in case none of the pivotings worked, but this is likely to be impossible.
            std::cout << "Pivoting (somehow) needs enhancement..." << std::endl;
            return false;
        }
    }
    return false;
}

/**
 * That's the main function, where the program starts
 * @param argc Amount of arguments
 * @param argv Array with arguments
 * @return returns 0 when program finishes
 */
int main (int argc, const char * argv[]) {
    // Working with the first argument. It should be within a given range to choose the
    // correct puzzle difficulty. Right now, only the following ones are supported:
    // TABLE SIZES:
    // BEGINNER     :  9x9
    // INTERMEDIATE : 16x16
    // EXPERT       : 16x30

    // The offsets, and position in screen are all empiric and based on a monitor with:
    // Height : 1080px
    // Width : 3286px (2 monitors)
    // Firefox browser with 80% zoom.
    DIFFICULTY difficulty;
    int board_size_x = 9;
    int board_size_y = 9;
    if (argc > 1) {
        switch (atoi(argv[1])) {
            case 0:
                difficulty = BEGINNER;
                break;
            case 1:
                difficulty = INTERMEDIATE;
                board_size_x = 16;
                board_size_y = 16;
                break;
            case 2:
                difficulty = EXPERT;
                board_size_x = 30;
                board_size_y = 16;
                break;
            default:
                difficulty = BEGINNER;
                break;
        }
    } else {
        difficulty = BEGINNER;
    }
    std::cout << "Difficulty is: " << difficulty << std::endl;
    // Setting and initializing variables
    int Width = 0;
    int Height = 0;
    int Bpp = 0;
    int x, y;
    std::vector<std::vector<char>>* board = new std::vector<std::vector<char>>(board_size_y, std::vector<char>(board_size_x));
    std::vector<std::uint8_t> Pixels;
    Display *display;
    Window root;
    
    // Restart game by clicking on the board's smiling face
    // Smiling face's position
    x=150+(board_size_x-9)*12.666;
    y=265;
    display = XOpenDisplay(nullptr);
    root = DefaultRootWindow(display);
    Screen* s = DefaultScreenOfDisplay(display);
    std::cout << "Screen's height is: " << s->height << std::endl;
    std::cout << "Screen's width is: " << s->width << std::endl;
    // Move pointer to the position desired
    XWarpPointer (display, None, root, 0,0,0,0, x, y);
    // Needs to flush in order to do the movement
    XFlush(display);
    // Close the display whenever it was previously opened by XOpenDisplay function
    XCloseDisplay(display);
    // Finally, clicks with the left mouse button. Use the mask to ensure only the left button is pressed.
    mouseClick(Button1, 0x001);
    // Wait game to restart
    sleep(1);

    // Force first click to start a new game
    // This is the initial position for click
    // TODO: make the initial position random
    x=100;
    y=345;
    // Same routine for moving pointer, and then click with left mouse button
    display = XOpenDisplay(nullptr);
    root = DefaultRootWindow(display);
    XWarpPointer (display, None, root, 0,0,0,0, x, y);
    XFlush(display);
    XCloseDisplay(display);
    mouseClick(Button1, 0x001);
    // Wait the game to be generated and started
    sleep(2);

    // With the game created, let's collect the image from it
    ImageFromDisplay(Pixels, Width, Height, Bpp);
    Mat img = Mat(Height, Width, Bpp > 24 ? CV_8UC4 : CV_8UC3, &Pixels[0]);

    // Create the board for ease the search for bombs and safe-tiles
    int i = 0;
    int j = 0;
    // TODO: These offsets are empiric (Firefox w/ 80% zoom). Needs to be updated to work regardless the
    //       table size
    // This nested-for run through every pixel in a row in order to collect the non-gray colors. Once the tile counter
    // reaches its stop count, then set the tile number based on the non-gray average pixels.
    for (int offset_row=318; offset_row < 530+(board_size_y-9)*25; offset_row += 25) {
        j = 0;
        int tile_counter = 0;
        Vec4b real_color = {0, 0, 0, 0};
        int counter = 0;
        for (int offset=34; offset<260+(board_size_x-9)*25; offset += 1) {
            // The iteration occurs for every tile's center.
            int x = offset;
            int y = offset_row;

            // Get the current pixel color
            Vec4b current_color = img.at<Vec4b>(y,x);

            // If tile counter is 25, or if it reaches the end of the row, then set the color.
            if (tile_counter == 25 || ((260+(board_size_x-9)*25)) - offset <= 1) {
                tile_counter = 0;
                COLOR color_verdict;
                if (counter > 0) {
                    color_verdict = colorIdentifier(real_color);
                    printf("NEW Position: %i %i at x:%i y:%i : %i, %i, %i, %i, VERDICT: %i\n", i+1, j+1, x, y, real_color.val[3],real_color.val[2],real_color.val[1], real_color.val[0], color_verdict);
                } else {
                    // In case counter was never increased, it means only grey pixels were collected.
                    // Force tile as a light gray
                    color_verdict = LIGHT_GRAY;
                    printf("NEW Position: %i %i at x:%i y:%i :FORCED LIGHT GRAY, VERDICT: %i\n", i+1, j+1, x, y, color_verdict);
                }
                real_color = {0, 0, 0, 0};
                counter = 0;
                // If it was a color, then just set it in the board. Otherwise, check if the tile is one that was clicked
                // or not.
                if (color_verdict) {
                    board->at(i).at(j) = (char)(48+color_verdict);
                } else {
                    // Need to know if tile was cliked, or not... The distinguishment will be done based on
                    // the information that an unclicked-tile has a white pixel range on its border.
                    // Begin at the tile's border
                    int old_x = x;
                    x = 34+25*j;
                    while(x <= 34+25*(j+1)) {
                        // Redo the average routine and color identification
                        Vec4b color_intermediate = pixelAverage(img, x, y, 0);
                        color_verdict = colorIdentifier(color_intermediate);
                        printf("INTERMEDIATE Position: %i %i at x:%i y:%i : %i, %i, %i, %i, VERDICT: %i\n", i+1, j+1, x, y, color_intermediate.val[3],color_intermediate.val[2],color_intermediate.val[1], color_intermediate.val[0], color_verdict);

                        // If this a white pixel, it means this is an unclicked tile.
                        if (color_verdict == WHITE) {
                            board->at(i).at(j) = 'E';
                            break;
                        } else if (34+25*(j+1)-x < 1) {
                            board->at(i).at(j) = '0';
                            break;
                        }
                        x++;
                    }
                    x = old_x;
                }
                j++;
            } else {
                if (current_color.val[2] > 110 && current_color.val[1] > 110 && current_color.val[0] > 110) {
                    // That's a light grey, skip it to avoid influenciate in the average.
                    tile_counter++;
                    continue;
                } else {
                    counter++;
                    for (int aux = 0; aux < 4; aux++) {
                        // That's a continuous way to get the average throughout the for-loop. Given Vec4b limitations,
                        // this approach was chosen in despite of only dividing the values at the end of the tile counter
                        real_color.val[aux] = (real_color.val[aux]*(counter-1) + current_color.val[aux])/(counter);
                    }
                }
            }
            tile_counter++;
        }
        i++;
    }

    // Print the initial parsed board
    std::cout << "Initial parsed board:" << std::endl;
    printBoard(*board);
    std::cout << std::endl;

    // This vector contains the tiles which were visited, and which there's nothing else to do with them.
    // So, we'll avoid visit them after they are done.
    std::vector<std::vector<int>> visited;
    std::vector<std::vector<int>> pivots_visited;
    bool board_stalled = false;
    // This major for-loop works for run over the board multiple times
    for (int k = 0; k < 50; k++) {
        int board_changes = 0;
        for (int i = 0; i < board_size_y; i++) {
            for (int j = 0; j < board_size_x; j++) {
                // No need to check tiles undiscovered (E), empty (0) or with bombs marked (M)
                // std::cout << "Checking board with i: " << i << " and j: " << j << std::endl;
                if (board->at(i).at(j) != 'E' && board->at(i).at(j) != '0' && board->at(i).at(j) != 'M') {
                    // Do not check tiles whieh are inside the visited-array
                    std::vector<int> aux = {i, j};
                    if (std::find(visited.begin(), visited.end(), aux) == visited.end() || 
                        board_stalled && std::find(pivots_visited.begin(), pivots_visited.end(), aux) == pivots_visited.end()) {
                        // Let's try to mark some bombs, or free tiles
                        STRATEGY strategy = board_stalled ? PIVOT : SIMPLE;
                        if (markBombs(*board, i, j, strategy)) {
                            // In case the board was updated, then check if the tile is no-longer needed,
                            // and if so, add it to visited-array
                            if (strategy == SIMPLE) {
                                auto after_marking = surroundingTiles(*board, i, j, {'E'});
                                std::cout << "After checking/marking position " << i+1 << " " << j+1 << " the amount of E's is: " << std::to_string(after_marking.size()) << std::endl;
                                if (after_marking.size() == 0) visited.emplace_back(aux);
                            } else if (strategy == PIVOT) {
                                // Pivoting worked! Let's empty the list, because it can led to other
                                // pivots to work now. Moreover, the board is no longer stalled (at least in first glance).
                                pivots_visited = {};
                                std::cout << "Emptying the pivots_visited vector" << std::endl;
                                board_stalled = false;
                            }
                            board_changes++;
                        } else {
                            if (strategy == PIVOT) {
                                // Pivoting failed. Add it to the visited list.
                                pivots_visited.emplace_back(aux);
                            }
                        }
                    }
                }
            }
        }
        if (!board_changes) {
            // Throughout an entire board run, nothing was changed with the SIMPLE strategy.
            // Let's switch to the PIVOT one.
            board_stalled = true;
        }
    }

    // Print the final board.
    std::cout << "Final board!" << std::endl;
    updateBoard(*board);
    std::cout << std::endl;

    // TODO: Return 1 if the board contains any 'E', or if a Bomb was caught.
    // Returning 0 must only occur when the game is finished with victory.
    return 0;
}