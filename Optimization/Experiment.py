import subprocess
import statistics
from bayes_opt import BayesianOptimization
from bayes_opt import UtilityFunction
from bayes_opt.logger import JSONLogger
from bayes_opt.event import Events

# Bounded region of parameter space
# Change bounds if needed but don't change the name of the variable.
INPUT_RANGE = {'spin_count': (100, 300000)}

OUTPUT_BASE_FOLDER = ".\\ThreadPivot"
PATH_TO_PRIME_NUMBERS_EXE = "C:\\Users\\musharm\\source\\repos\\SpinWaitSimulation\\PrimeNumbers\\x64\\Release\\PrimeNumbers.exe" 

# Prefer Exploitation = 0 and Prefer Exploration = 0.1
EXPLORATION_EXPLOITATION_PARAMETER = 0.1 

# For reproducibility.
SEED = 1

TOTAL_ITERATIONS = 200 

# Inner iterations to find some central value.
INNER_ITERATIONS = 5

# Iterations to give the algorithm a bit of help to understand the shape.
BURN_IN_ITERATIONS = 10

class OptimizationExperiment(object):

    def __init__(self, list_of_functions):
        self.list_of_functions = list_of_functions

    def __fix_json(self, data):
        split = data.split("\n")
        fixed = "" 
        last_line = split[-1]
        for line in split:
            if line == "":
                continue
            fixed += line + ","
        fixed = fixed[:-1]
        fixed = "[" + fixed + "]"
        return fixed
        
    def run(self):
        global EXPLORATION_EXPLOITATION_PARAMETER
        global PATH_TO_PRIME_NUMBERS_EXE
        global OUTPUT_BASE_FOLDER
        global TOTAL_ITERATIONS
        global BURN_IN_ITERATIONS 
        global INPUT_RANGE 
        global SEED 

        for f in self.list_of_functions:
            optimizer = BayesianOptimization(
                f=f,
                pbounds=INPUT_RANGE,
                random_state=SEED,)

            file_name = f"{OUTPUT_BASE_FOLDER}/{f.__name__}._incomplete.json"

            logger = JSONLogger(path = file_name)
            optimizer.subscribe(Events.OPTIMIZATION_STEP, logger)

            acquisition_function = UtilityFunction(kind="ei", xi=EXPLORATION_EXPLOITATION_PARAMETER)
            
            # Run the optimizer.
            optimizer.maximize(init_points=BURN_IN_ITERATIONS, n_iter= TOTAL_ITERATIONS, acquisition_function = acquisition_function)

            # Write out results after fixing JSON file..
            with open(file_name) as f:
                file_contents = f.read()
                data = (self.__fix_json(file_contents))
                output_file_name = file_name.replace("_incomplete", "") + ".json"
                with open(output_file_name, 'w+') as o:
                    o.write(data)

def pivot_thread_count(spin_count, thread_count):
    vals = []
    for i in range(INNER_ITERATIONS):
        DETACHED_PROCESS = 0x00000008
        process_name = PATH_TO_PRIME_NUMBERS_EXE 
        args = f" --input_count 30000 --complexity 20 --ht 0 --thread_count {thread_count} --join_type 3 --affi 0 --mwaitx_cycle_count {int(spin_count)}"
        cmd = process_name + args 
        print(f"{i}. Invoking: {cmd}") 
        results = subprocess.Popen(cmd, close_fds=True, creationflags=DETACHED_PROCESS, stdout=subprocess.PIPE) 
        out = str(results.communicate()[0]) 
        v = -float(out.split("|")[19]) 
        print(f"{i}. Invoking: {cmd} | {out}")
        vals.append(v)
    median = statistics.median(vals)
    print(f"Input: {spin_count} | Output: {median}")
    return median

def pivot_thread_count_2(spin_count):
    return pivot_thread_count(spin_count, 2)
def pivot_thread_count_4(spin_count):
    return pivot_thread_count(spin_count, 4)
def pivot_thread_count_8(spin_count):
    return pivot_thread_count(spin_count, 8)
def pivot_thread_count_16(spin_count):
    return pivot_thread_count(spin_count, 16)
def pivot_thread_count_32(spin_count):
    return pivot_thread_count(spin_count, 32)
def pivot_thread_count_64(spin_count):
    return pivot_thread_count(spin_count, 64)
def pivot_thread_count_126(spin_count):
    return pivot_thread_count(spin_count, 126)

pivot_threads = [ pivot_thread_count_2, pivot_thread_count_4, pivot_thread_count_8, pivot_thread_count_16, pivot_thread_count_32, pivot_thread_count_64, pivot_thread_count_126 ]

def run_thread_count_pivot_join_type_3():
    experiment = OptimizationExperiment(pivot_threads)
    experiment.run()

if __name__ == '__main__':
    run_thread_count_pivot_join_type_3()