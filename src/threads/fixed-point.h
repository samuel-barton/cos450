
#define f (1 << 14)





// Addition
int add_fp_fp(int x, int y);
int add_fp_int(int x, int n);
// Subtraction
int subtract_fp_fp(int x, int y); 
int subtract_fp_int(int x, int n);
// Multiplication
int multiply_fp_fp(int x, int y);
int multiply_fp_int(int x, int n); 
// Division 
int divide_fp_fp(int x, int y);
int divide_fp_int(int x, int n);
// Conversion
int int_to_fp(int n);
int fp_to_int_down(int x);
int fp_to_int_nearest(int x); 

int 
add_fp_fp(int x, int y)
{
  return x + y;
}

int 
add_fp_int(int x, int n)
{
  return x + n * f;
}


int 
subtract_fp_fp(int x, int y)
{
  return x - y;
}
 
int 
subtract_fp_int(int x, int n)
{
  return x - n * f; 
}

int 
multiply_fp_fp(int x, int y){
  return ((int64_t) x) * y / f ; 
}

int 
multiply_fp_int(int x, int n)
{
  return x * n;
} 

int 
divide_fp_fp(int x, int y)
{
  return ((int64_t) x) * f / y;
}

int 
divide_fp_int(int x, int n)
{
  return x / n;
}

int 
int_to_fp(int n)
{
  return n * f; 
}

int 
fp_to_int_down(int x)
{
  return x / f; 
}

int 
fp_to_int_nearest(int x)
{
  if ( x >= 0){
    return (x + f / 2 ) / f;
  }
  return (x - f / 2 ) / f;
  
}






