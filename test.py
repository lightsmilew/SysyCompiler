compilerPath = "myCompiler/build/my_compiler"
inputDir = "test_cases/semantic_cases/"
outputDir = "test_cases/output/"
inputDir = "test_cases/official_case/"
outputDir = "test_cases/official_output/"

cases = inputDir + "*.sy"

import os
import glob
import subprocess

def run_test_case(case):
    output_file = os.path.join(outputDir, os.path.basename(case) + ".out")
    with open(output_file, "w") as out:
        result = subprocess.run([compilerPath, case,"-ir"], stdout=out, stderr=subprocess.STDOUT)
        return result.returncode

def main():
    if not os.path.exists(outputDir):
        os.makedirs(outputDir)

    test_cases = glob.glob(cases)
    if not test_cases:
        print("No test cases found.")
        return
    for case in test_cases:
        print(f"Running test case: {case}")
        return_code = run_test_case(case)
        if return_code == 0:
            print(f"Test case {case} passed.")
        else:
            print(f"Test case {case} failed with return code {return_code}.")

if __name__ == "__main__":
    main()