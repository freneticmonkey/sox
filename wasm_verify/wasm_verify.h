#ifndef WASM_VERIFY_H
#define WASM_VERIFY_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verify a WASM file by loading and executing it with wazero.
 *
 * @param path Path to the .wasm file
 * @param output Pointer to receive comma-separated output values (caller must free with FreeString)
 * @param errorMsg Pointer to receive error message on failure (caller must free with FreeString)
 * @return 1 on success, 0 on failure
 */
int VerifyWASMFile(const char* path, char** output, char** errorMsg);

/**
 * Get WASM output as an array of doubles.
 *
 * @param path Path to the .wasm file
 * @param output Pointer to receive array of double values (caller must free with FreeDoubleArray)
 * @param count Pointer to receive number of values in output array
 * @param errorMsg Pointer to receive error message on failure (caller must free with FreeString)
 * @return 1 on success, 0 on failure
 */
int GetWASMOutput(const char* path, double** output, int* count, char** errorMsg);

/**
 * Free a string allocated by Go.
 *
 * @param s String to free
 */
void FreeString(char* s);

/**
 * Free a double array allocated by Go.
 *
 * @param arr Array to free
 */
void FreeDoubleArray(double* arr);

#ifdef __cplusplus
}
#endif

#endif // WASM_VERIFY_H
