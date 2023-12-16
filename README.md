# arduino_5_drum_interface_module

## Game Design / Flow

 - ~~Insert token~~ (push button) to start the game!
 - Game starts couting down (4s)
 - First hit! SCISSOR starts to rise (ID4 REG0 2)
 - The tutorial section: The round time doesn't change but the score increases (DONE)
 - Once the scissor lift reaches the top (ID4 REG0 2)
    -   Hard mode!
    -   Pause for a bit? And flash a message on the screen
    -   Trigger the big flamethrower (ID3 COIL5 TRUE)
    -   Transfer individual hits to SNAKEHEADS (ID1) from SAXAPHONES (ID3)
    -   On each 5th hit set the SAXAPHONES off instead of the SNAKEHEADS
        -   5 &rArr;  COIL 1
        -   10 &rArr; COIL 1 4
        -   15 &rArr; COIL 1 2 4
        -   20 &rArr; COIL 1 2 3 4
        -   30 &rArr; COIL 5
        -   35 &rArr; COIL 1 5
        -   40 &rArr; COIL 1 4 5
        -   45 &rArr; COIL 1 2 4 5 
        -   50+ &rArr; COIL 1 2 3 4 5
        -   HIGHSCORE SNAKEBODY wiggle and excited blasting!
- On failure!
    - Reset SCISSOR
    - Reset SNAKEHEADS
    - Reset SNAKEBODYS
- On reset!
    - Re-enable game start!

- IDLE/BUSKING animations?