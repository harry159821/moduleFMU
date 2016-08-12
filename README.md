#moduleFMU
This is a module to import and cosimulation of FMUs, developed using FMI standards, for MBDyn.

#Requirement
1. MBDyn simulation software. (can be downloaded from https://www.mbdyn.org/?Software\_Download)
2. This module is created by using FMILibrary. FMILibrary is a JModellica software  that enables integration of Functional Mock-up Units (FMUs) import in applications. It can be downloaded from http://www.jmodelica.org/FMILibrary
3. In Makefile.inc from modules/module-FMU/ in mbdyn-VERSION, 

	FMIL: Location where build directory of FMILibrary exists.

	FMII: Location where FMILibrary is installed. 

#Input Syntax 
user defined: \<label\>, FMU, “\<location to FMU\>”, <br />
	type, \<cosimulation/import\>, tolerance, \<tolerance value\>, <br />
	\<fmu input variable\>, node, \<node label\>, \<node type\>, \<mbdyn equivalent\>, <br />
	\<fmu input variable\>, node, \<node label\>, \<node type\>, \<mbdyn equivalent\>, <br />
	..... <br />
	output, yes; <br />

Example Usage: <br />
user defined: 99, FMU, "/location/to/BouncingBall\_cs.fmu", <br />
type, cosimulation, <br />
"u", node, 1, structural, string, "X[3]", direct, <br />
"u2", node, 1, structural, string, "X[2]", direct, <br />
output, yes; <br />

#Limitations


