
#include "instruct.h"
#include "memory.h"

void init_instruct(instruct* instructs)
{
    instructs->count = 0;
    instructs->capacity = 0;
	instructs->current = 0;
    instructs->code = NULL;
}

void reset_instruct(instruct *instructs)
{
    for (int i = 0; i < instructs->capacity; ++i)
        FREE(code8*, instructs->code[i]);
    FREE(code8**, instructs->code);
	init_instruct(instructs);
}
