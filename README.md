# Jacks Compass - *To get you 🫵 to a safe harbor*
## Inspiration
After recently rewatching several pirates of the carribean movies, we were thinking about the sheer utility value of Jack Sparrows compass. Its ability to point you to whatever you most desire is immensly useful, though with the vague parameters of "whatever the user desires" it's rather impractical to implement in the real world.  
Decomposing the problem to the most common use case, as the compass is owned by the sea, it would be rather entertaining to imagine a person in posession of the compass becoming stranded. In such a situation, their most vaunted desire would be a safe harbor or port to return to society from.  
The ability to find a safe harbor from anywhere in the world without need for internet or cellular is a great boon.
## What it does
The esp32 board is flashed with a list of ports, harbors, boat ramps, and other common water accesses. With the help of a gps antenna and a magnetometer, the compass is able to determine your current position and heading. With that information it calculates the direction to the nearest port/water access to your location relative to the direction you're facing. Using a stepper motor, it then turns a dial to indicate that direction to you so that you know where that safe harbor is in relation to yourself.
## How we built it
We initially decided on using a esp32 board with a magnetometer, stepper motor, and gps antenna. Since the goal of the project was to be able to navigate to a safe harbor from anywhere, we had to find a dataset that we could query ahead of time and store on the esp32.  
We ended up deciding to scrape overpass for a list of water accesses. For our initial product, we've only queried for water accesses that are in the US pacific northwest (top right corner is just southwest of Seattle and bottom right is a little into Nevada).  
We precompiled the list and hardcoded it into the source-code for now, though we also set up pipelining to be able to generate the list directly into our code directory. The file containing the ports also has code to take in the compass' current position and determine the 5 closest ports, this way if the closest port to the users position is inaccessible or blocked, they can switch to finding the next closest port at the press of a button.  
We soldered and connected the sensors to our esp32 board and generated boilerplate code for reading the signals they sent to get the variables we needed.  
With the position, heading, and direction to the nearest port, all it took was a bit of trigonometry to determine which direction the stepper motor needed to face to show the user the way to safety.
Since the inspiration was jack sparrow's compass, we decided to 3d print a housing based off the same compass to store the project in and made a little sundial within to do the pointing.  
## Challenges we encountered
#### Prevalence of arduino code caused a mixup
One member of our group has a lot more experience with arduino code than esp32, and accidentally referenced the wrong set of libraries when writing the boilerplate code to extract the sensor input.
#### Wiring issues
Solder kept falling, one port ended up getting completely filled with solder, fortunately it was the ground port on the gpd unit, which has a redundant ground port that we were able to make use of.
#### GPS issues
Due to the buildings that this event was hosted in having rather thick walls, and the gps unit we were using being rather weak, we ende dup getting little to no reading under most tests, and with the fragility of the testing rig we weren't able to do proper outdoor tests until things had been finalized to a point where we wouldn't be able to change anything about the gps if it didn't work.
#### Sensor issues
Use of dupont wires for early testing led to inconsistient readings, which made it hard to tell if the testing was going well until after we made permanent connections. Additionally, all the sensors used in this project are particularly affected by environmental conditions, namely being inside of a building. While not to the extent of the GPS unit, which was bad enough it got an entire issue dedicated to it, the magnetometer also had very inconsistient readings while within KEC and the Toyota floor of reser stadium.
#### 3d printed housing broke
After getting permission to pre-print the housing for our project, it turned out that the cad file we used to create the recess for the stepper motor to stay in was the wrong size. We filed it down to make the stepper motor fit for testing purposes, but in the process, the hinges for both halves of the compass broke off. We reprinted the casing roughly halfway through the day after catching this in time.
#### Data storage
After expanding our definition of a "safe port" to ensure that we'd have a fair amount of data points to test the compass on, we realized that our algorithms for determining the nearest one were rather clunky. We ended up simplifying the data structures used in order to reduce the ram overhead on the esp32, but were able to get everything to work in the end.
#### lack of access to 3d printer after starting print
While we caught our issues with our casing rather early in the day, we used the damlab to print out the new versions of the files. Under normal circumstances, this would be fine, but with a time crunch in place, it caused a host of issues. Our prints finished after the damlab closed to access, so we weren't able to pick them up until the following morning, only 3 hours before judging began.
## Accomplishments
After all the issues with the sensor readings, when we finally got them all to work right, it felt amazing that the math to determine the distances and headings worked first try and we didn't have to redo the calculations.  

## Lessons
Be more careful with cadding, pre-plan places to put sensors in hardware housing, use proper holders for hardware when soldering.
## Upgrades
Due to the way that we're storing the port locations on the esp32, we didn't end up trying out collating all global ports. We aqcuired a sd card to attach to the board if we needed the memory from it, but since we never got around to seeing how taxing the whole dataset would be, we never integrated it.  
If we have more time and energy to put towards this project, we'll probably integrate a global dataset for the compass to act on. Though if we do, we'd probably need to go back to using a more efficient sorting algorithm for the nearest function.