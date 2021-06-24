#include<bits/stdc++.h>

enum token{
    tok_eof = -1,
    
    tok_def = -2,
    tok_extern = -3,

    tok_identifier = -4,
    tok_number = -5,

};

static std::string IdentifierStr;
static double NumVal;

static int gettok() {
    static int LastChar = ' ';

    // Skip any whitespace.
    while (isspace(LastChar))
       LastChar = getchar();
    
    if(isalpha(LastChar)) {  // identifier: [a-zA-Z][a-zA-Z0-9]*
        IdentifierStr = LastChar;
        while(isalnum((LastChar = getchar())))
            IdentifierStr += LastChar;
        
        if (IdentifierStr == "def")
            return tok_def;
            
        if (IdentifierStr == "extern")
            return tok_extern;

        return tok_identifier;
    }

    if(isdigit(LastChar) || LastChar == '.'){ // Number: [0-9.]+
        std::string NumStr;

        do{
            NumStr += LastChar;
            LastChar = getchar();
        }while(isdigit(LastChar) || LastChar == '.');

        NumVal = strtod(NumStr.c_str(),0);
        return tok_number;
    }

    //comment handling
    if (LastChar == '#'){
        do
        {
            LastChar = getchar();
        } while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

        if (LastChar !=EOF)
            return gettok();
    }

    //check for EOF
    if(LastChar == EOF)
        return tok_eof;
    
    //otherwise, just return the character as it's ascii value.
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

// int main(){
//     while(true){
//         int tok = gettok();
//         std::cout<<"got token: "<<tok<<"\n";
//     }
// }