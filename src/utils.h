#ifndef sh
#define sh


// function to split string into arbitrary number of strings based on delimiter
// returns array of strings
char** splitString(char* str, const char* delim, unsigned int* numTokens);

// replace the first occurrence of subStr with newSubStr in *dest in place
int replaceString(char** dest, const char* subStr, const char* newSubStr);

int randInt(int min, int max);

#endif //sh
