#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MEMORY_SIZE 30000
#define STACK_SIZE 1000

// Instruction set (8-bit binary)
typedef enum {
    INSTR_MOVE_R = 0b00000001,   // Move pointer right
    INSTR_MOVE_L = 0b00000010,   // Move pointer left
    INSTR_INC     = 0b00000100,   // Increment current cell
    INSTR_DEC     = 0b00001000,   // Decrement current cell
    INSTR_OUT     = 0b00010000,   // Output current cell
    INSTR_IN      = 0b00100000,   // Input to current cell
    INSTR_JMP_F   = 0b01000000,   // Jump forward if current cell is 0
    INSTR_JMP_B   = 0b10000000,   // Jump back if current cell is not 0
    INSTR_NOP     = 0b00000000,   // No operation
    INSTR_HALT    = 0b11111111    // Halt execution
} Instruction;

// Error codes
typedef enum {
    ERR_OK,
    ERR_UNCLOSED_COMMENT,
    ERR_EMPTY_COMMENT,
    ERR_INVALID_BINARY,
    ERR_INSTRUCTION_NOT_8BIT,
    ERR_UNMATCHED_JUMP,
    ERR_STACK_OVERFLOW,
    ERR_STACK_UNDERFLOW,
    ERR_MEMORY_OVERFLOW,
    ERR_MEMORY_UNDERFLOW,
    ERR_HALTED
} ErrorCode;

// Parser state
typedef struct {
    unsigned char *code;
    int code_len;
    int ip;
    unsigned char *memory;
    int ptr;
    int *jump_stack;
    int jump_sp;
    int line;
    int col;
    int in_comment;
    int comment_start_line;
    int comment_start_col;
    int auto_newline;
    int last_char;
} BinaryVM;

// Initialize VM
BinaryVM* vm_create(void) {
    BinaryVM *vm = malloc(sizeof(BinaryVM));
    vm->code = NULL;
    vm->code_len = 0;
    vm->ip = 0;
    vm->memory = calloc(MEMORY_SIZE, 1);
    vm->ptr = MEMORY_SIZE / 2;
    vm->jump_stack = malloc(sizeof(int) * STACK_SIZE);
    vm->jump_sp = -1;
    vm->line = 1;
    vm->col = 1;
    vm->in_comment = 0;
    vm->comment_start_line = 0;
    vm->comment_start_col = 0;
    vm->auto_newline = 1;
    vm->last_char = 0;
    return vm;
}

// Free VM
void vm_free(BinaryVM *vm) {
    if (!vm) return;
    free(vm->code);
    free(vm->memory);
    free(vm->jump_stack);
    free(vm);
}

// Load binary file
ErrorCode vm_load(BinaryVM *vm, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return ERR_INVALID_BINARY;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    vm->code = malloc(size);
    vm->code_len = fread(vm->code, 1, size, f);
    fclose(f);
    
    // Verify each byte is 8-bit
    for (int i = 0; i < vm->code_len; i++) {
        if (vm->code[i] > 0xFF) {
            return ERR_INSTRUCTION_NOT_8BIT;
        }
    }
    
    return ERR_OK;
}

// Parse comments (very strict)
ErrorCode parse_comment(BinaryVM *vm, const char *input, int *pos) {
    if (input[*pos] != ';') return ERR_OK;
    
    // Comment start
    if (!vm->in_comment) {
        vm->in_comment = 1;
        vm->comment_start_line = vm->line;
        vm->comment_start_col = vm->col;
        (*pos)++;
        
        // Check if comment is empty
        if (input[*pos] == ';') {
            return ERR_EMPTY_COMMENT;
        }
        return ERR_OK;
    }
    
    // Comment end
    if (vm->in_comment) {
        // Check if there was content before
        if (*pos > 0 && input[*pos - 1] == ';') {
            return ERR_EMPTY_COMMENT;
        }
        
        vm->in_comment = 0;
        (*pos)++;
        return ERR_OK;
    }
    
    return ERR_OK;
}

// Execute
ErrorCode vm_run(BinaryVM *vm) {
    while (vm->ip < vm->code_len) {
        unsigned char instr = vm->code[vm->ip];
        
        // Update position
        vm->col++;
        if (instr == '\n') {
            vm->line++;
            vm->col = 1;
        }
        
        switch (instr) {
            case INSTR_MOVE_R:
                if (vm->ptr >= MEMORY_SIZE - 1) return ERR_MEMORY_OVERFLOW;
                vm->ptr++;
                break;
                
            case INSTR_MOVE_L:
                if (vm->ptr <= 0) return ERR_MEMORY_UNDERFLOW;
                vm->ptr--;
                break;
                
            case INSTR_INC:
                vm->memory[vm->ptr]++;
                break;
                
            case INSTR_DEC:
                vm->memory[vm->ptr]--;
                break;
                
            case INSTR_OUT: {
                char c = vm->memory[vm->ptr];
                putchar(c);
                vm->last_char = c;
                break;
            }
                
            case INSTR_IN:
                vm->memory[vm->ptr] = getchar();
                break;
                
            case INSTR_JMP_F:
                if (vm->jump_sp >= STACK_SIZE - 1) return ERR_STACK_OVERFLOW;
                if (vm->memory[vm->ptr] == 0) {
                    int depth = 1;
                    while (depth > 0) {
                        vm->ip++;
                        if (vm->ip >= vm->code_len) return ERR_UNMATCHED_JUMP;
                        if (vm->code[vm->ip] == INSTR_JMP_F) depth++;
                        if (vm->code[vm->ip] == INSTR_JMP_B) depth--;
                    }
                } else {
                    vm->jump_stack[++vm->jump_sp] = vm->ip;
                }
                break;
                
            case INSTR_JMP_B:
                if (vm->jump_sp < 0) return ERR_STACK_UNDERFLOW;
                if (vm->memory[vm->ptr] != 0) {
                    vm->ip = vm->jump_stack[vm->jump_sp];
                } else {
                    vm->jump_sp--;
                }
                break;
                
            case INSTR_HALT:
                // Auto-newline at program end
                if (vm->auto_newline && vm->last_char != '\n') {
                    putchar('\n');
                }
                return ERR_HALTED;
                
            case INSTR_NOP:
                break;
                
            default:
                // Non-instruction bytes (comments or other)
                break;
        }
        
        vm->ip++;
    }
    
    // Auto-newline at normal program end
    if (vm->auto_newline && vm->last_char != '\n') {
        putchar('\n');
    }
    
    return ERR_OK;
}

// Run binary file
ErrorCode run_binary_file(const char *filename) {
    BinaryVM *vm = vm_create();
    ErrorCode err = vm_load(vm, filename);
    
    if (err != ERR_OK) {
        printf("Load error: %d\n", err);
        vm_free(vm);
        return err;
    }
    
    err = vm_run(vm);
    
    if (err != ERR_OK && err != ERR_HALTED) {
        printf("\nExecution error: %d at line %d column %d\n", err, vm->line, vm->col);
    }
    
    vm_free(vm);
    return err;
}

// Compile text to binary
ErrorCode compile_to_binary(const char *input_file, const char *output_file) {
    FILE *in = fopen(input_file, "r");
    if (!in) return ERR_INVALID_BINARY;
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fclose(in);
        return ERR_INVALID_BINARY;
    }
    
    BinaryVM *vm = vm_create();
    char line[1024];
    int in_comment = 0;
    int line_num = 0;
    
    while (fgets(line, sizeof(line), in)) {
        line_num++;
        int len = strlen(line);
        
        for (int i = 0; i < len; i++) {
            if (line[i] == ';') {
                if (!in_comment) {
                    // Comment start
                    in_comment = 1;
                    // Check if next character is ; (empty comment)
                    if (i + 1 < len && line[i + 1] == ';') {
                        printf("Error: Empty comment at line %d\n", line_num);
                        fclose(in);
                        fclose(out);
                        vm_free(vm);
                        return ERR_EMPTY_COMMENT;
                    }
                } else {
                    // Comment end
                    in_comment = 0;
                }
                continue;
            }
            
            // Skip content inside comments
            if (in_comment) continue;
            
            // Only process 0 and 1
            if (line[i] == '0' || line[i] == '1') {
                unsigned char byte = 0;
                // Read 8 bits
                for (int b = 0; b < 8; b++) {
                    byte <<= 1;
                    if (i + b < len && line[i + b] == '1') {
                        byte |= 1;
                    }
                }
                fwrite(&byte, 1, 1, out);
                i += 7;  // Skip processed 7 bits
            }
        }
    }
    
    // Check for unclosed comments
    if (in_comment) {
        printf("Error: Unclosed comment at line %d\n", line_num);
        fclose(in);
        fclose(out);
        vm_free(vm);
        return ERR_UNCLOSED_COMMENT;
    }
    
    fclose(in);
    fclose(out);
    vm_free(vm);
    return ERR_OK;
}

// Main function
int main(int argc, char *argv[]) {
    int auto_newline = 1;
    
    if (argc < 2) {
        printf("Usage:\n");
        printf("  binary run <file.bin>          - Execute binary file\n");
        printf("  binary run -n <file.bin>       - Execute binary file (no auto-newline)\n");
        printf("  binary compile <file.txt>      - Compile text to binary\n");
        printf("  binary compile <file.txt> -o <file.bin>\n");
        return 1;
    }
    
    if (strcmp(argv[1], "run") == 0) {
        const char *filename;
        
        if (argc >= 3 && strcmp(argv[2], "-n") == 0) {
            auto_newline = 0;
            if (argc < 4) {
                printf("Error: Missing filename\n");
                return 1;
            }
            filename = argv[3];
        } else if (argc >= 3) {
            filename = argv[2];
        } else {
            printf("Error: Missing filename\n");
            return 1;
        }
        
        BinaryVM *vm = vm_create();
        vm->auto_newline = auto_newline;
        
        ErrorCode err = vm_load(vm, filename);
        if (err != ERR_OK) {
            printf("Load error: %d\n", err);
            vm_free(vm);
            return 1;
        }
        
        err = vm_run(vm);
        
        if (err != ERR_OK && err != ERR_HALTED) {
            printf("\nExecution error: %d at line %d column %d\n", err, vm->line, vm->col);
            vm_free(vm);
            return 1;
        }
        
        vm_free(vm);
    }
    else if (strcmp(argv[1], "compile") == 0 && argc >= 3) {
        const char *output = "a.bin";
        if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
            output = argv[4];
        }
        ErrorCode err = compile_to_binary(argv[2], output);
        if (err == ERR_OK) {
            printf("Compile successful: %s -> %s\n", argv[2], output);
        } else {
            return 1;
        }
    }
    else {
        printf("Unknown command\n");
        return 1;
    }
    
    return 0;
}
