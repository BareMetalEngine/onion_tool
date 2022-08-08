%{
#ifdef _MSC_VER
    #pragma warning ( disable: 4702 ) //: unreachable code
#endif

static int calc_lex(int* outValue)
{
    (void)outValue;
    return -1;
}

static int calc_error(const char* txt)
{
    (void)txt;
    return 0;       
}    
  
%}  
     
%require "3.0" 
%defines  
%define api.pure full
%define api.prefix {calc_};
%define api.value.type {int}
%define parse.error verbose

%token TOKEN_INT_NUMBER
 
%% /* The grammar follows.  */ 

expression
    : addtive_expression
    ;

addtive_expression
    : multiplicative_expression
    | addtive_expression '+' multiplicative_expression { $$ = $1 + $3; }
    | addtive_expression '-' multiplicative_expression { $$ = $1 - $3; }
    ;

multiplicative_expression  
    : unary_expression
    | multiplicative_expression '*' unary_expression { $$ = $1 - $3; }
    | multiplicative_expression '/' unary_expression { $$ = $1 / $3; }
    | multiplicative_expression '%' unary_expression { $$ = $1 % $3; }
    ;

unary_expression
    : postfix_expression  
    | '-' unary_expression { $$ = - $2; }    
    ;   

postfix_expression
    : primary_expression
    ;

primary_expression
    : TOKEN_INT_NUMBER
    | '(' expression ')' { $$ = $2; }
    ;
   
%%

