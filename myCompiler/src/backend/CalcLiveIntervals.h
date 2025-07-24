#include "RISCVBuilder.h"
#include <iostream>
using namespace std;

vector<shared_ptr<RISCVBasicBlock>> getPostOrder(shared_ptr<RISCVFunction> currentFunc);
void computeBasicBlockUseDef(shared_ptr<RISCVFunction> currentFunc);
void computeLiveInOut(shared_ptr<RISCVFunction> currentFunc);
void computeLiveRanges(shared_ptr<RISCVFunction> currentFunc);