EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 4 4
Title ""
Date ""
Rev ""
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L OPL_Discrete_Semiconductor:SMD-MOSFET-N-CH-20V-2.1A-CJ2302_SOT-23_ Q1
U 1 1 5F866449
P 5475 4000
AR Path="/5F86634F/5F866449" Ref="Q1"  Part="1" 
AR Path="/5F867776/5F866449" Ref="Q2"  Part="1" 
AR Path="/5F867929/5F866449" Ref="Q3"  Part="1" 
F 0 "Q3" H 5590 4042 45  0000 L CNN
F 1 "SMD-MOSFET-N-CH-20V-2.1A-CJ2302_SOT-23_" H 5590 3958 45  0000 L CNN
F 2 "MyFootprints:SOT-416-Hand_soldering" H 5475 4000 50  0001 C CNN
F 3 "" H 5475 4000 50  0001 C CNN
F 4 "CJ2302" H 5505 4150 20  0001 C CNN "MPN"
F 5 "305030015" H 5505 4150 20  0001 C CNN "SKU"
	1    5475 4000
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR011
U 1 1 5F866507
P 5475 4550
AR Path="/5F86634F/5F866507" Ref="#PWR011"  Part="1" 
AR Path="/5F867776/5F866507" Ref="#PWR013"  Part="1" 
AR Path="/5F867929/5F866507" Ref="#PWR015"  Part="1" 
F 0 "#PWR015" H 5475 4300 50  0001 C CNN
F 1 "GND" H 5480 4377 50  0000 C CNN
F 2 "" H 5475 4550 50  0001 C CNN
F 3 "" H 5475 4550 50  0001 C CNN
	1    5475 4550
	1    0    0    -1  
$EndComp
Text HLabel 4575 4000 0    50   Input ~ 0
Motor_PWM
Wire Wire Line
	5275 4000 5075 4000
Wire Wire Line
	5475 4200 5475 4350
$Comp
L Motor:Motor_DC M1
U 1 1 5F867580
P 5475 3450
AR Path="/5F86634F/5F867580" Ref="M1"  Part="1" 
AR Path="/5F867776/5F867580" Ref="M2"  Part="1" 
AR Path="/5F867929/5F867580" Ref="M3"  Part="1" 
F 0 "M3" H 5633 3446 50  0000 L CNN
F 1 "Motor_DC" H 5633 3355 50  0000 L CNN
F 2 "MyFootprints:Vibrating_5x6mm_DC_motor" H 5475 3360 50  0001 C CNN
F 3 "~" H 5475 3360 50  0001 C CNN
	1    5475 3450
	1    0    0    -1  
$EndComp
Wire Wire Line
	5475 3150 5475 3250
Wire Wire Line
	5475 3750 5475 3800
$Comp
L Device:R R4
U 1 1 5F88A7FD
P 5075 4150
AR Path="/5F86634F/5F88A7FD" Ref="R4"  Part="1" 
AR Path="/5F867776/5F88A7FD" Ref="R5"  Part="1" 
AR Path="/5F867929/5F88A7FD" Ref="R6"  Part="1" 
F 0 "R6" H 5145 4196 50  0000 L CNN
F 1 "10K" H 5145 4105 50  0000 L CNN
F 2 "Resistor_SMD:R_0603_1608Metric_Pad0.98x0.95mm_HandSolder" V 5005 4150 50  0001 C CNN
F 3 "~" H 5075 4150 50  0001 C CNN
	1    5075 4150
	1    0    0    -1  
$EndComp
Connection ~ 5075 4000
Wire Wire Line
	5075 4000 4575 4000
Wire Wire Line
	5075 4300 5075 4350
Wire Wire Line
	5075 4350 5475 4350
Connection ~ 5475 4350
Wire Wire Line
	5475 4350 5475 4550
$Comp
L power:+3.3V #PWR012
U 1 1 60ED8357
P 5475 3150
AR Path="/5F86634F/60ED8357" Ref="#PWR012"  Part="1" 
AR Path="/5F867776/60ED8357" Ref="#PWR014"  Part="1" 
AR Path="/5F867929/60ED8357" Ref="#PWR017"  Part="1" 
F 0 "#PWR017" H 5475 3000 50  0001 C CNN
F 1 "+3.3V" H 5490 3323 50  0000 C CNN
F 2 "" H 5475 3150 50  0001 C CNN
F 3 "" H 5475 3150 50  0001 C CNN
	1    5475 3150
	1    0    0    -1  
$EndComp
$EndSCHEMATC
