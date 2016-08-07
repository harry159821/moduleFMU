#include "module-FMU.h"
#include "solver.h"

//#define DEBUG 

ModuleFMU::ModuleFMU(
	unsigned uLabel, const DofOwner *pDO,
	DataManager* pDM, MBDynParser& HP)
: Elem(uLabel, flag(0)),
UserDefinedElem(uLabel, pDO),
pDM(pDM)
{
/*  Reading from the input file     */
	if (HP.IsKeyWord("help")) {
		silent_cout("help text here"  << std::endl);

        	if (!HP.IsArg()) {
  	        	throw NoErr(MBDYN_EXCEPT_ARGS);
	        }
        }

        strcpy(FMUlocation, HP.GetStringWithDelims());
	
	std::string UClocation;
	UClocation = UncompressLocation(FMUlocation);

	if(HP.IsKeyWord("type")){
		simType = HP.GetString();
		if (!strcmp(simType, "cosimulation")){
			SIMTYPE = COSIM;
		} 
		else if (!strcmp(simType, "import")){
			SIMTYPE = IMPORT;
		}
		else {
			silent_cout("Unsupported Simulation Type. "
					"Available options:\n"
					"cosimulation\n"
					"import model\n"
					"Exiting...");
		}
	} else {
		silent_cerr("keyword \"type\" expected");
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}

	double relativeTolerance;
	if(HP.IsKeyWord("tolerance")){
		relativeTolerance = HP.GetReal();
	} else {	
		relativeTolerance = 0.001;
	}

	while(HP.IsStringWithDelims()){
		const char* temp = HP.GetStringWithDelims(); 
		drivesContainer[temp] = HP.GetDriveCaller();
	}

	SetOutputFlag(pDM->fReadOutput(HP, Elem::LOADABLE));
/* End of reading from input file         */

	if(SIMTYPE == COSIM){
		silent_cout("Model defined as co-simulation.\n");
	} else if ( SIMTYPE == IMPORT ){
		silent_cout("Model defined as import.\n");
	}

	setup_callbacks(&callbacks);

/// Unzip the fmu
        status = fmi_zip_unzip(FMUlocation, UClocation.c_str(), &callbacks);
	
	if(status==jm_status_error){
		silent_cerr("Failed to uncompress FMU. Exiting\n");	
		throw ErrGeneric(MBDYN_EXCEPT_ARGS);
	}
	else
		silent_cout("FMU uncompressed successfully \n");

/// Get the version and context
        context = fmi_import_allocate_context(&callbacks);
        version = fmi_import_get_fmi_version(context, FMUlocation, UClocation.c_str());

	if(version == 1){
		model = new fmu1(context);
	} else if (version == 2){
		model = new fmu2(context);
	}

	silent_cout("Version"<<version);
        model->parseXML(context, UClocation.c_str());
        model->setCallBackFunction();
	model->ImportCreateDLL(SIMTYPE);

	currTime    = pDM->dGetTime();
	initialTime = currTime;
	endTime     = (pDM->GetSolver())->GetDFinalTime();
/// SIMTYPE specific work

	if(SIMTYPE == IMPORT){

		double dTol = pDM->GetSolver()->pGetStepIntegrator()->GetIntegratorDTol();
		model->Initialize(dTol, currTime, relativeTolerance);
		model->EventIndicatorInit();
		numOfContinousStates = model->GetNumOfContinousStates();
		numOfEventIndicators = model->GetNumOfEventIndicators();

	} else if (SIMTYPE == COSIM ) {

		model->InitializeAsSlave(UClocation.c_str(), initialTime, endTime);
		silent_cout("Initialized as slave\n");
		timeStep = pDM->GetSolver()->GetDInitialTimeStep();

	}

	currState           = new double[numOfContinousStates];
	directionalFlag     = model->SupportsDirectionalDerivatives(SIMTYPE);
	jacobianInputVector = new int[drivesContainer.size()];
	privDriveLength = 0;
	int k = 0;

	for (strDriveCon::iterator i = drivesContainer.begin(); i != drivesContainer.end(); i++){

		if(!(model->CheckInput(i->first))){
			silent_cout("Variable "<<i->first<<" is not of type input\n");
			drivesContainer.erase(i->first);

		} else {
			if (dynamic_cast<const PrivDriveCaller*>(i->second) != NULL){
				jacobianInputVector[privDriveLength] = model->GetRefValueFromString((i->first).c_str());
				privDrivesIndex[privDriveLength] = dynamic_cast<const PrivDriveCaller*>(i->second);
				privDriveLength ++;
			}
			k++;
		}

		if (drivesContainer.size() == 0){
			silent_cout("No FMU input was defined in input file. \n");
			break;
		}
	}
	
	if (directionalFlag){
		jacobian = new double*[numOfContinousStates];
		for (int i=0; i<numOfContinousStates; i++)
			jacobian[i] = new double[numOfContinousStates + privDriveLength];

		seedVector = new double[numOfContinousStates + privDriveLength];
	}
}

ModuleFMU::~ModuleFMU(void)
{
	if (SIMTYPE == IMPORT)
		model->Terminate();
	else if (SIMTYPE == SIMTYPE)
		model->TerminateSlave();

	if(directionalFlag){
		for(int i=0; i<numOfContinousStates; i++)
			delete[] jacobian[i];
		delete[] jacobian;
		delete[] seedVector;
	}

	delete[] currState;
	delete[] jacobianInputVector;

	drivesContainer.clear();
}


void
ModuleFMU::Output(OutputHandler& OH) const
{
	if (bToBeOutput()) {
		std::ostream& out = OH.Loadable();
		for(int i  = 0;  i < numOfContinousStates; ++i){
			out << std::setw(4) << currState[i] << " ";
		}
		out<<std::endl;
	}
}


DofOrder::Order
ModuleFMU::GetEqType(unsigned int i) const
{
        return DofOrder::DIFFERENTIAL;
}



void
ModuleFMU::WorkSpaceDim(integer* piNumRows, integer* piNumCols) const
{
	*piNumRows = numOfContinousStates;
	*piNumCols = numOfContinousStates;
}


VariableSubMatrixHandler& 
ModuleFMU::AssJac(VariableSubMatrixHandler& WorkMat,
	doublereal dCoef, 
	const VectorHandler& XCurr,
	const VectorHandler& XPrimeCurr)
{
	WorkMat.SetNullMatrix();
#ifdef DEBUG
	silent_cout(__func__);
#endif

	if (numOfContinousStates > 0){
		FullSubMatrixHandler& WM = WorkMat.SetFull();
		WM.ResizeReset(numOfContinousStates, numOfContinousStates + privDriveLength);
		integer iFirstIndex = iGetFirstIndex();

		{
			int i = 1;
			for ( i=1; i<=numOfContinousStates ; i++){
				WM.PutRowIndex(i, iFirstIndex + i);
				WM.PutColIndex(i, iFirstIndex + i);
			}

			for (int j = 0; j <privDriveLength; j++){
				WM.PutColIndex(i, privDrivesIndex[j]->iGetIndex());
				i++;
			}
		}
		
		if (directionalFlag){


			for (int i=0; i<numOfContinousStates; i++)
				seedVector[i] = currState[i];
			for (int i=0; i<privDriveLength; i++)
				seedVector[i+numOfContinousStates] = privDrivesIndex[i]->dGet();

			model->GetDirectionalDerivatives(jacobian, jacobianInputVector, privDriveLength, seedVector);
			for (int i=0; i<numOfContinousStates; i++){
				WM.IncCoef(i,i, 1);
				for(int j=0; j<numOfContinousStates; j++)
					WM.IncCoef(i,j, -dCoef*jacobian[i][j]);
				
				for(int j=numOfContinousStates; j<numOfContinousStates + privDriveLength; j++){
					if(privDrivesIndex[j-numOfContinousStates]->iGetSE()->
						GetDofType(privDrivesIndex[i-numOfContinousStates]->iGetIndex()) 
							== DofOrder::DIFFERENTIAL)
						WM.IncCoef(i,j, -dCoef*jacobian[i][j]);
					else
						WM.IncCoef(i,j, -jacobian[i][j]);
				}
			}

			delete[] seedVector;

		} else {
			for(int i=1; i<=numOfContinousStates; i++)
				WM.IncCoef(i , i, 1.);
		}
	}

	return WorkMat;
}

SubVectorHandler& 
ModuleFMU::AssRes(SubVectorHandler& WorkVec,
	doublereal dCoef,
	const VectorHandler& XCurr, 
	const VectorHandler& XPrimeCurr)
{

	WorkVec.ResizeReset(0);
#ifdef DEBUG
	silent_cout(__func__);
#endif
	for (strDriveCon::iterator i = drivesContainer.begin(); i != drivesContainer.end(); i++){
		model->SetValuesByVariable(i->first, (i->second)->dGet());
	}

	if(SIMTYPE == IMPORT){
		WorkVec.ResizeReset(numOfContinousStates);
		if (currTime != pDM->dGetTime()){
			
			currTime = pDM->dGetTime();

			int iFirstIndex = iGetFirstIndex();
				
			//Get Current States
			for (int i=0; i<numOfContinousStates; i++){
				currState[i] = XCurr(iFirstIndex + i + 1);
			}
	
			model->SetTime(currTime);
			model->SetStates(currState);	
//			model->CheckInterrupts(currTime);
			
			stateDerivatives = model->GetStateDerivatives();
		}
		
		//Get Index of the elements
		integer iFirstIndex = iGetFirstIndex();

		//Set Index to WorkVec
		for (int i=1; i<=numOfContinousStates; i++)
			WorkVec.PutRowIndex(i, iFirstIndex + i);

		//Set WorkVec with the difference in the XPrimCurr - FMUDerivative
		for (int i=1; i<=numOfContinousStates; i++){
			WorkVec.PutCoef(i, (stateDerivatives[i-1] - XPrimeCurr(i + iFirstIndex)));
		}	
	}

	if (SIMTYPE==COSIM){
		model->CSPropogate(pDM->dGetTime(), timeStep);
	}
	
	return WorkVec;
}

unsigned int
ModuleFMU::iGetNumPrivData(void) const
{
	return model->GetNumOfVar();
}

unsigned int
ModuleFMU::iGetPrivDataIdx(const char *s) const
{
	unsigned int idx = 0;

#ifdef DEBUG	
	silent_cout(__func__);
#endif

	idx = model->GetRefValueFromString(s);
	idx = idx + 1;

	return idx;
}

doublereal
ModuleFMU::dGetPrivData(unsigned int i) const
{
	return 	model->GetStateFromRefValue(i-1);
}

int
ModuleFMU::iGetNumConnectedNodes(void) const
{
	return 0;
}

void
ModuleFMU::GetConnectedNodes(std::vector<const Node *>& connectedNodes) const
{
	NO_OP;
}

void
ModuleFMU::SetValue(DataManager *pDM,
	VectorHandler& X, VectorHandler& XP,
	SimulationEntity::Hints *ph)
{
	if(numOfContinousStates > 0){
		if(SIMTYPE == IMPORT){
			currState = model->GetStates();
			stateDerivatives = model->GetStateDerivatives();
			for (int i=0; i<numOfContinousStates; i++){
				X(iGetFirstIndex() + i + 1) = currState[i];
				XP(iGetFirstIndex() + i + 1) = stateDerivatives[i];
			}
		}
	}
}

std::ostream&
ModuleFMU::Restart(std::ostream& out) const
{
	return out << "# ModuleFMU: not implemented" << std::endl;
}

unsigned int 
ModuleFMU::iGetNumDof(void) const
{
	if (SIMTYPE == IMPORT)
		return numOfContinousStates;
	else if (SIMTYPE == COSIM)
		return 0;
}

DofOrder::Order
ModuleFMU::GetDofType(unsigned int i) const
{
        return DofOrder::DIFFERENTIAL;
}


unsigned int
ModuleFMU::iGetInitialNumDof(void) const
{
	return 0;
}

void 
ModuleFMU::InitialWorkSpaceDim(
	integer* piNumRows,
	integer* piNumCols) const
{
	*piNumRows = 0;
	*piNumCols = 0;
}

VariableSubMatrixHandler&
ModuleFMU::InitialAssJac(
	VariableSubMatrixHandler& WorkMat, 
	const VectorHandler& XCurr)
{
	// should not be called, since initial workspace is empty
	ASSERT(0);

	WorkMat.SetNullMatrix();

	return WorkMat;
}

SubVectorHandler& 
ModuleFMU::InitialAssRes(
	SubVectorHandler& WorkVec,
	const VectorHandler& XCurr)
{
	// should not be called, since initial workspace is empty
	ASSERT(0);

	WorkVec.ResizeReset(0);

	return WorkVec;
}

extern "C" int
module_init(const char *module_name, void *pdm, void *php)
{
	UserDefinedElemRead *rf1 = new UDERead<ModuleFMU>;

	if (!SetUDE("FMU", rf1)) {
		delete rf1;

		silent_cerr("ModuleFMU: "
			"module_init(" << module_name << ") "
			"failed" << std::endl);

		return -1;
	}

	return 0;
}
