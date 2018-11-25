float basis_function_b_val ( float tdata[], float tval );
float basis_function_beta_val ( float beta1, float beta2, float tdata[],
  float tval );
float *basis_matrix_b_uni ( );
float *basis_matrix_beta_uni ( float beta1, float beta2 );
float *basis_matrix_bezier ( );
float *basis_matrix_hermite ( );
float *basis_matrix_overhauser_nonuni ( float alpha, float beta );
float *basis_matrix_overhauser_nul ( float alpha );
float *basis_matrix_overhauser_nur ( float beta );
float *basis_matrix_overhauser_uni ( void);
float *basis_matrix_overhauser_uni_l ( );
float *basis_matrix_overhauser_uni_r ( );
float basis_matrix_tmp ( int left, int n, float mbasis[], int ndata,
  float tdata[], float ydata[], float tval );
void bc_val ( int n, float t, float xcon[], float ycon[], float *xval,
  float *yval );
float bez_val ( int n, float x, float a, float b, float y[] );
float bpab_approx ( int n, float a, float b, float ydata[], float xval );
float *bp01 ( int n, float x );
float *bpab ( int n, float a, float b, float x );
int chfev ( float x1, float x2, float f1, float f2, float d1, float d2,
  int ne, float xe[], float fe[], int next[] );
int d3_fs ( float a1[], float a2[], float a3[], int n, float b[], float x[] );
float *d3_mxv ( int n, float a[], float x[] );
float *d3_np_fs ( int n, float a[], float b[] );
void d3_print ( int n, float a[], char *title );
void d3_print_some ( int n, float a[], int ilo, int jlo, int ihi, int jhi );
float *d3_uniform ( int n, int *seed );
void data_to_dif ( int ntab, float xtab[], float ytab[], float diftab[] );
float dif_val ( int ntab, float xtab[], float diftab[], float xval );
int i4_max ( int i1, int i2 );
int i4_min ( int i1, int i2 );
void least_set ( int point_num, float x[], float f[], float w[],
  int nterms, float b[], float c[], float d[] );
float least_val ( int nterms, float b[], float c[], float d[],
  float x );
void least_val2 ( int nterms, float b[], float c[], float d[], float x,
  float *px, float *pxp );
void least_set_old ( int ntab, float xtab[], float ytab[], int ndeg,
  float ptab[], float b[], float c[], float d[], float *eps, int *ierror );
float least_val_old ( float x, int ndeg, float b[], float c[], float d[] );
void parabola_val2 ( int ndim, int ndata, float tdata[], float ydata[],
  int left, float tval, float yval[] );
float pchst ( float arg1, float arg2 );
float r8_abs ( float x );
float r8_max ( float x, float y );
float r8_min ( float x, float y );
float r8_uniform_01 ( int *seed );
float *r8ge_fs_new ( int n, float a[], float b[] );
void r8vec_bracket ( int n, float x[], float xval, int *left, int *right );
void r8vec_bracket3 ( int n, float t[], float tval, int *left );
float *r8vec_even_new ( int n, float alo, float ahi );
float *r8vec_indicator_new ( int n );
int r8vec_order_type ( int n, float x[] );
void r8vec_print ( int n, float a[], char *title );
void r8vec_sort_bubble_a ( int n, float a[] );
float *r8vec_uniform_new ( int n, float b, float c, int *seed );
int r8vec_unique_count ( int n, float a[], float tol );
void r8vec_zero ( int n, float a[] );
float spline_b_val ( int ndata, float tdata[], float ydata[], float tval );
float spline_beta_val ( float beta1, float beta2, int ndata, float tdata[],
  float ydata[], float tval );
float spline_constant_val ( int ndata, float tdata[], float ydata[],
  float tval );
float *spline_cubic_set ( int n, float t[], float y[], int ibcbeg,
  float ybcbeg, int ibcend, float ybcend );
float *penta ( int n, float a1[], float a2[], float a3[], float a4[],
  float a5[], float b[] );
float spline_cubic_val ( int n, float t[], float y[], float ypp[],
  float tval, float *ypval, float *yppval );
void spline_cubic_val2 ( int n, float t[], float tval, int *left, float y[],
  float ypp[], float *yval, float *ypval, float *yppval );
float *spline_hermite_set ( int ndata, float tdata[], float ydata[],
  float ypdata[] );
void spline_hermite_val ( int ndata, float tdata[], float c[], float tval,
  float *sval, float *spval );
float spline_linear_int ( int ndata, float tdata[], float ydata[], float a,
  float b );
void spline_linear_intset ( int int_n, float int_x[], float int_v[],
  float data_x[], float data_y[] );
void spline_linear_val ( int ndata, float tdata[], float ydata[],
  float tval, float *yval, float *ypval );
float spline_overhauser_nonuni_val ( int ndata, float tdata[],
  float ydata[], float tval );
float spline_overhauser_uni_val ( int ndata, float tdata[], float ydata[],
  float tval );
void spline_overhauser_val ( int ndim, int ndata, float tdata[], float ydata[],
  float tval, float yval[] );
void spline_pchip_set ( int n, float x[], float f[], float d[] );
void spline_pchip_val ( int n, float x[], float f[], float d[], int ne,
  float xe[], float fe[] );
void spline_quadratic_val ( int ndata, float tdata[], float ydata[],
  float tval, float *yval, float *ypval );
void timestamp ( void );
