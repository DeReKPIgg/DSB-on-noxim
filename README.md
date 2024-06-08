# Special Project on Electrical Engineering

This is an electrical engineering project of National Yang Ming Chiao Tung University. 
We reimplemented structure of distributed shared-buffer router by adapting source code of Noxim and compare the performance with Noxim's original input buffer router structure.

## Distributed Shared-Buffer Reimplementation

The Input Buffer Router (IBR) and Distributed Shared-Buffer (DSB) router have three and five pipeline stage for hardware design, respectively. In Noxim, IBR has been reduced to two stages with merely RX and TX stage, and all computation is in two-phase TX stage. Hence, we also reduce DSB into three stages.

![image](/img/DSB.jpg "DSB architecture")

### RX stage

A flit from one direction arrives in the corresponding slot in the first in first out (FIFO) input buffer (IB) with its own VC ID, which is essentially identical to the IBR process.

### IB2MM stage

After the routing algorithm is performed. We check if there is a valid VC for each flit. Preventing the arrival conflict by assigning only one flit to one MM in a cycle. And, departure conflict is avoided by assigning staggered timestamp so that we won’t have flits that depart through same output port at the same cycle. If all the conditions above are met, the flit in the IB is moved to the corresponding MM slot.

### TX stage 

We can pop a whole column of flits in the MM to the output ports with respect to the current timestamp. At last, circular shift the router timestamp.

## Experiment Result

We simulate three architectures with three kinds of traffic patterns, and try to find the saturation point of each condition by the soaring average delay.
Since the delay function in Noxim only considers those received instead of all generated flits, the delay might be underestimated with a low received/ideal flits ratio. Therefore, we redefine the average delay function and set it to the maximum value (total cycles) as a penalty if the flit cannot reach the destination.
By defining the saturation as the point that delay = 500 cycles, we can observe that the postponement of saturation is the most obvious in the Random traffic. As the plot shows, 5-MM and 10-MM DSBs outperform IBR by 17.19% and 30.43% respectively. There are also visible progress in the Bit Reversal case, they improve 4.89% and 7.73% respectively on the maximum affordable injection rate. However, the performance only changes by 2.22% and 2.66% in the Transpose2 case.

### Random Traffic

![image1](/img/Random.png "Random")

### Bit_Reversal Traffic

![image2](/img/Bit_Reversal.png "Bit_Reversal")

### Transpose2 Traffic

![image3](/img/Transpose2.png "Transpose2")


## Installation & Quick Start

If you are working on Ubuntu, you can install noxim and all the dependencies with the following command:
(**BE sure of copying the entire line, i.e., ending with "ubuntu.sh"**)

    bash <(wget -qO- --no-check-certificate https://raw.githubusercontent.com/davidepatti/noxim/master/other/setup/ubuntu.sh)

Similarly for macOS:

    /bin/zsh -c "$(curl -fsSL https://raw.githubusercontent.com/davidepatti/noxim/master/other/setup/macos.zsh)"

Or, to get just the latest master sources, you can run:

    git clone https://github.com/davidepatti/noxim.git

To get DSB code, you can run:

    git clone https://github.com/DeReKPIgg/DSB-on-noxim.git

## Generate Simulation Result

Go to directory of Noxim/bin/:

    ./noxim -config ../config_examples/DSBonNoxim.yaml > sim_test.out

## Custom Settings

By changing 'traffic_distribution' in yaml file, you can choose different traffic type.\
By changing 'packet_injection_rate' in yaml file, you can modify injection rate.

## Citation

**Noxim**

Using Noxim (a netwok on chip simulator) , thanks to V. Catania, A. Mineo, S. Monteleone, M. Palesi, D. Patti. Noxim: An Open, Extensible and Cycle-accurate Network on Chip Simulator. IEEE International Conference on Application-specific Systems, Architectures and Processors 2015. July 27-29, 2015, Toronto, Canada.

Link to noxim original source-code: https://github.com/davidepatti/noxim

V. Catania, A. Mineo, S. Monteleone, M. Palesi and D. Patti, "Improving the energy efficiency of wireless Network on Chip architectures through online selective buffers and receivers shutdown," 2016 13th IEEE Annual Consumer Communications & Networking Conference (CCNC), Las Vegas, NV, 2016, pp. 668-673, doi: 10.1109/CCNC.2016.7444860.

[Scopus reference](https://www.scopus.com/record/display.uri?eid=2-s2.0-84966659566&origin=resultslist&sort=plf-f&src=s&sid=b531296d946a78b05f463c35c681a44c&sot=autdocs&sdt=autdocs&sl=18&s=AU-ID%2835610853000%29&relpos=14&citeCnt=6&searchTerm=)

V. Catania, A. Mineo, S. Monteleone, M. Palesi and D. Patti, "Energy efficient transceiver in wireless Network on Chip architectures," 2016 Design, Automation & Test in Europe Conference & Exhibition (DATE), Dresden, 2016, pp. 1321-1326.
[Scopus reference](https://www.scopus.com/record/display.uri?eid=2-s2.0-84973661681&origin=resultslist&sort=plf-f&src=s&sid=4bd3ffce04cc0093a84655249383aefa&sot=autdocs&sdt=autdocs&sl=18&s=AU-ID%2835610853000%29&relpos=11&citeCnt=11&searchTerm=)

**Distributed Shared-Buffer NoC Router**

 Rohit Sunkam Ramanujam∗, Vassos Soteriou†, Bill Lin∗ and Li-Shiuan Peh‡. Design of a High-Throughput Distributed Shared-Buffer NoC Router. 2010 Fourth ACM/IEEE International Symposium on Networks-on-Chip\
 ∗Department of Electrical and Computer Engineering, University of California, San Diego\
 †Department of Electrical Engineering and Information Technology, Cyprus University of Technology\
 ‡Department of Electrical Engineering and Computer Science, Massachusetts Institute of Technology

## Registration

To receive information about new Noxim features, updates and events, please register here:
[Registration Form](https://docs.google.com/forms/d/e/1FAIpQLSfJnYQZwxC4gr4jUc-nuwuGp0MDBA-0N_TVf8hqV1DIa325Dg/viewform?c=0&w=1)

