#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MEMORY_SIZE 30000
#define STACK_SIZE 1000
#define MAX_LINE_LENGTH 1024
#define MAX_COMMENT_LENGTH 512

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

// More detailed error codes
typedef enum {
    ERR_OK = 0,
    ERR_UNCLOSED_COMMENT,
    ERR_EMPTY_COMMENT,
    ERR_COMMENT_NO_ALNUM,        // Comment has no alphanumeric characters
    ERR_COMMENT_TOO_LONG,        // Comment exceeds maximum length
    ERR_INVALID_BINARY,
    ERR_INSTRUCTION_NOT_8BIT,
    ERR_UNMATCHED_JUMP,
    ERR_STACK_OVERFLOW,
    ERR_STACK_UNDERFLOW,
    ERR_MEMORY_OVERFLOW,
    ERR_MEMORY_UNDERFLOW,
    ERR_HALTED,
    ERR_FILE_NOT_FOUND,
    ERR_FILE_TOO_LARGE,
    ERR_INVALID_CHARACTER,       // Invalid character in source
    ERR_LINE_TOO_LONG,           // Line exceeds maximum length
    ERR_BINARY_NOT_8BIT,         // Binary string not exactly 8 bits
    ERR_MISSING_HALT,            // Program missing HALT instruction
    ERR_TOO_MANY_INSTRUCTIONS,   // Too many instructions for memory
    ERR_INVALID_WHITESPACE,      // Invalid whitespace usage
    ERR_CONSECUTIVE_COMMENTS,    // Consecutive comment blocks
    ERR_COMMENT_OUTSIDE,         // Comment outside valid positions
} ErrorCode;

// Error messages
const char* error_messages[] = {
    [ERR_OK] = "No error",
    [ERR_UNCLOSED_COMMENT] = "Unclosed comment",
    [ERR_EMPTY_COMMENT] = "Empty comment",
    [ERR_COMMENT_NO_ALNUM] = "Comment must contain at least one alphanumeric character",
    [ERR_COMMENT_TOO_LONG] = "Comment too long",
    [ERR_INVALID_BINARY] = "Invalid binary file",
    [ERR_INSTRUCTION_NOT_8BIT] = "Instruction not 8-bit",
    [ERR_UNMATCHED_JUMP] = "Unmatched jump instruction",
    [ERR_STACK_OVERFLOW] = "Jump stack overflow",
    [ERR_STACK_UNDERFLOW] = "Jump stack underflow",
    [ERR_MEMORY_OVERFLOW] = "Memory overflow (pointer too far right)",
    [ERR_MEMORY_UNDERFLOW] = "Memory underflow (pointer too far left)",
    [ERR_HALTED] = "Program halted",
    [ERR_FILE_NOT_FOUND] = "File not found",
    [ERR_FILE_TOO_LARGE] = "File too large",
    [ERR_INVALID_CHARACTER] = "Invalid character in source (only 0, 1, ;, whitespace allowed)",
    [ERR_LINE_TOO_LONG] = "Line exceeds maximum length",
    [ERR_BINARY_NOT_8BIT] = "Binary string must be exactly 8 bits",
    [ERR_MISSING_HALT] = "Program must end with HALT instruction",
    [ERR_TOO_MANY_INSTRUCTIONS] = "Too many instructions",
    [ERR_INVALID_WHITESPACE] = "Invalid whitespace (use spaces or newlines only)",
    [ERR_CONSECUTIVE_COMMENTS] = "Consecutive comments not allowed",
    [ERR_COMMENT_OUTSIDE] = "Comment must be on its own line or after instruction",
};

// VM state
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
    int has_halt;               // Check if HALT instruction exists
    int instruction_count;      // Count instructions for validation
} BinVM;

// Initialize VM
BinVM* vm_create(void) {
    BinVM *vm = (BinVM*)malloc(sizeof(BinVM));
    if (!vm) return NULL;
    
    vm->code = NULL;
    vm->code_len = 0;
    vm->ip = 0;
    vm->memory = (unsigned char*)calloc(MEMORY_SIZE, 1);
    vm->ptr = MEMORY_SIZE / 2;
    vm->jump_stack = (int*)malloc(sizeof(int) * STACK_SIZE);
    vm->jump_sp = -1;
    vm->line = 1;
    vm->col = 1;
    vm->in_comment = 0;
    vm->comment_start_line = 0;
    vm->comment_start_col = 0;
    vm->auto_newline = 1;
    vm->last_char = 0;
    vm->has_halt = 0;
    vm->instruction_count = 0;
    
    return vm;
}

// Free VM
void vm_free(BinVM *vm) {
    if (!vm) return;
    free(vm->code);
    free(vm->memory);
    free(vm->jump_stack);
    free(vm);
}

// Load binary file
ErrorCode vm_load(BinVM *vm, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return ERR_FILE_NOT_FOUND;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Check file size
    if (size > 1024 * 1024) {  // Max 1MB
        fclose(f);
        return ERR_FILE_TOO_LARGE;
    }
    
    vm->code = (unsigned char*)malloc(size);
    if (!vm->code) {
        fclose(f);
        return ERR_FILE_TOO_LARGE;
    }
    
    vm->code_len = fread(vm->code, 1, size, f);
    fclose(f);
    
    // Verify each byte is 8-bit and check for HALT
    vm->has_halt = 0;
    for (int i = 0; i < vm->code_len; i++) {
        if (vm->code[i] > 0xFF) {
            return ERR_INSTRUCTION_NOT_8BIT;
        }
        if (vm->code[i] == INSTR_HALT) {
            vm->has_halt = 1;
        }
    }
    
    return ERR_OK;
}

// Check if character is valid in source
int is_valid_source_char(char c) {
    return (c == '0' || c == '1' || c == ';' || c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

// Check if string contains alphanumeric characters
int has_alphanumeric(const char *str, int len) {
    for (int i = 0; i < len; i++) {
        if (isalnum((unsigned char)str[i])) {
            return 1;
        }
    }
    return 0;
}

// Check if string contains only whitespace
int is_only_whitespace(const char *str, int len) {
    for (int i = 0; i < len; i++) {
        if (!isspace((unsigned char)str[i])) {
            return 0;
        }
    }
    return 1;
}

// Parse comments (very strict)
ErrorCode parse_comment(BinVM *vm, const char *line, int *pos, int line_num, int *in_comment) {
    if (line[*pos] != ';') return ERR_OK;
    
    if (!*in_comment) {
        // Comment start
        *in_comment = 1;
        vm->comment_start_line = line_num;
        vm->comment_start_col = *pos + 1;
        (*pos)++;
        
        // Check if comment is empty or invalid
        int start = *pos;
        
        // Find comment end
        while (line[*pos] && line[*pos] != ';') {
            (*pos)++;
        }
        
        if (!line[*pos]) {
            return ERR_UNCLOSED_COMMENT;
        }
        
        // Now at ';' - extract comment content
        int content_len = *pos - start;
        if (content_len == 0) {
            return ERR_EMPTY_COMMENT;
        }
        
        // Check if content has alphanumeric characters
        if (!has_alphanumeric(&line[start], content_len)) {
            return ERR_COMMENT_NO_ALNUM;
        }
        
        // Check comment length
        if (content_len > MAX_COMMENT_LENGTH) {
            return ERR_COMMENT_TOO_LONG;
        }
        
        // End comment
        *in_comment = 0;
        (*pos)++;  // Skip ending ;
        
        return ERR_OK;
    }
    
    return ERR_OK;
}

// Validate binary string
ErrorCode validate_binary_string(const char *str, int len) {
    if (len != 8) {
        return ERR_BINARY_NOT_8BIT;
    }
    
    for (int i = 0; i < 8; i++) {
        if (str[i] != '0' && str[i] != '1') {
            return ERR_INVALID_CHARACTER;
        }
    }
    
    return ERR_OK;
}

// Compile text to binary
ErrorCode compile_to_binary(const char *input_file, const char *output_file) {
    FILE *in = fopen(input_file, "r");
    if (!in) return ERR_FILE_NOT_FOUND;
    
    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fclose(in);
        return ERR_FILE_NOT_FOUND;
    }
    
    char line[MAX_LINE_LENGTH];
    int line_num = 0;
    int in_comment = 0;
    int comment_line = 0;
    int instruction_count = 0;
    int has_halt = 0;
    int last_was_comment = 0;
    
    while (fgets(line, sizeof(line), in)) {
        line_num++;
        int len = strlen(line);
        
        // Remove trailing newline for processing
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            len--;
        }
        if (len > 0 && line[len-1] == '\r') {
            line[len-1] = '\0';
            len--;
        }
        
        // Check line length
        if (len >= MAX_LINE_LENGTH - 1) {
            fclose(in);
            fclose(out);
            return ERR_LINE_TOO_LONG;
        }
        
        // Check for invalid characters
        for (int i = 0; i < len; i++) {
            if (!is_valid_source_char(line[i])) {
                fclose(in);
                fclose(out);
                printf("Invalid character '%c' (ASCII %d) at line %d, column %d\n", 
                       line[i], line[i], line_num, i+1);
                return ERR_INVALID_CHARACTER;
            }
        }
        
        // Skip empty lines
        if (len == 0) continue;
        
        // Check if line is all whitespace
        int all_whitespace = 1;
        for (int i = 0; i < len; i++) {
            if (!isspace((unsigned char)line[i])) {
                all_whitespace = 0;
                break;
            }
        }
        if (all_whitespace) continue;
        
        int i = 0;
        
        // Parse comments
        while (i < len) {
            if (line[i] == ';') {
                ErrorCode err = parse_comment(NULL, line, &i, line_num, &in_comment);
                if (err != ERR_OK) {
                    fclose(in);
                    fclose(out);
                    return err;
                }
                
                if (in_comment) {
                    comment_line = line_num;
                }
            } else {
                // Skip whitespace in binary lines
                if (isspace((unsigned char)line[i])) {
                    i++;
                    continue;
                }
                
                // If we're in a comment, this is an error
                if (in_comment) {
                    fclose(in);
                    fclose(out);
                    printf("Binary data inside comment at line %d\n", line_num);
                    return ERR_COMMENT_OUTSIDE;
                }
                
                // Found binary data
                if (i + 8 <= len) {
                    // Validate binary string
                    ErrorCode err = validate_binary_string(&line[i], 8);
                    if (err != ERR_OK) {
                        fclose(in);
                        fclose(out);
                        return err;
                    }
                    
                    // Convert to byte
                    unsigned char byte = 0;
                    for (int b = 0; b < 8; b++) {
                        byte <<= 1;
                        if (line[i + b] == '1') {
                            byte |= 1;
                        }
                    }
                    
                    // Check for HALT
                    if (byte == INSTR_HALT) {
                        has_halt = 1;
                    }
                    
                    fwrite(&byte, 1, 1, out);
                    instruction_count++;
                    
                    // Check instruction count
                    if (instruction_count > MEMORY_SIZE) {
                        fclose(in);
                        fclose(out);
                        return ERR_TOO_MANY_INSTRUCTIONS;
                    }
                    
                    i += 8;
                } else {
                    // Not enough characters for 8-bit instruction
                    fclose(in);
                    fclose(out);
                    return ERR_BINARY_NOT_8BIT;
                }
            }
        }
    }
    
    // Check for unclosed comments
    if (in_comment) {
        fclose(in);
        fclose(out);
        return ERR_UNCLOSED_COMMENT;
    }
    
    // Check for HALT instruction
    if (!has_halt) {
        fclose(in);
        fclose(out);
        return ERR_MISSING_HALT;
    }
    
    fclose(in);
    fclose(out);
    return ERR_OK;
}

// Execute VM
ErrorCode vm_run(BinVM *vm) {
    vm->ip = 0;
    vm->ptr = MEMORY_SIZE / 2;
    vm->jump_sp = -1;
    
    while (vm->ip < vm->code_len) {
        unsigned char instr = vm->code[vm->ip];
        
        // Update position for error reporting
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
                if (vm->auto_newline && vm->last_char != '\n') {
                    putchar('\n');
                }
                return ERR_HALTED;
                
            case INSTR_NOP:
                break;
                
            default:
                // Non-instruction bytes (should not happen in valid binary)
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
ErrorCode run_binary_file(const char *filename, int auto_newline) {
    BinVM *vm = vm_create();
    if (!vm) return ERR_FILE_NOT_FOUND;
    
    vm->auto_newline = auto_newline;
    
    ErrorCode err = vm_load(vm, filename);
    if (err != ERR_OK) {
        printf("Load error: %s\n", error_messages[err]);
        vm_free(vm);
        return err;
    }
    
    err = vm_run(vm);
    
    if (err != ERR_OK && err != ERR_HALTED) {
        printf("\nExecution error at line %d, column %d: %s\n", 
               vm->line, vm->col, error_messages[err]);
    }
    
    vm_free(vm);
    return err;
}

// Print usage
void print_usage(void) {
    printf("BinVM - Binary Virtual Machine (Strict Mode)\n");
    printf("Usage:\n");
    printf("  binvm compile <file.txt> [-o <file.bin>]  - Compile text to binary\n");
    printf("  binvm run <file.bin> [-n]                 - Execute binary file\n");
    printf("  binvm validate <file.txt>                 - Validate source file\n");
    printf("\nOptions:\n");
    printf("  -n    No auto-newline at program end\n");
    printf("\nStrict rules:\n");
    printf("  • Comments must be enclosed in ; and contain alphanumeric characters\n");
    printf("  • Binary instructions must be exactly 8 bits (0 or 1)\n");
    printf("  • Program must end with HALT instruction (11111111)\n");
    printf("  • Only characters allowed: 0, 1, ;, space, tab, newline\n");
    printf("  • No empty lines between comments and code\n");
}

// Validate source file
ErrorCode validate_source(const char *filename) {
    return compile_to_binary(filename, "/dev/null");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    if (strcmp(argv[1], "compile") == 0) {
        if (argc < 3) {
            printf("Error: Missing input file\n");
            return 1;
        }
        
        const char *output = "a.bin";
        if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
            output = argv[4];
        }
        
        ErrorCode err = compile_to_binary(argv[2], output);
        if (err == ERR_OK) {
            printf("Compile successful: %s -> %s\n", argv[2], output);
            return 0;
        } else {
            printf("Compile error: %s\n", error_messages[err]);
            return 1;
        }
    }
    else if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            printf("Error: Missing filename\n");
            return 1;
        }
        
        int auto_newline = 1;
        const char *filename = argv[2];
        
        if (argc >= 4 && strcmp(argv[3], "-n") == 0) {
            auto_newline = 0;
        }
        
        ErrorCode err = run_binary_file(filename, auto_newline);
        if (err != ERR_OK && err != ERR_HALTED) {
            return 1;
        }
        return 0;
    }
    else if (strcmp(argv[1], "validate") == 0) {
        if (argc < 3) {
            printf("Error: Missing filename\n");
            return 1;
        }
        
        ErrorCode err = validate_source(argv[2]);
        if (err == ERR_OK) {
            printf("Validation successful: %s is valid\n", argv[2]);
            return 0;
        } else {
            printf("Validation error: %s\n", error_messages[err]);
            return 1;
        }
    }
    else {
        printf("Unknown command: %s\n", argv[1]);
        print_usage();
        return 1;
    }
}
