/* The MIT License

   Copyright (c) 2011 by Attractive Chaos <attractor@live.co.uk>

   Permission is hereby granted, free of charge, to any person obtaining
   a copy of this software and associated documentation files (the
   "Software"), to deal in the Software without restriction, including
   without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to
   the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

// This is a reimplementation of Guenter Stertenbrink's suexco.c. For more details, see:
// http://magictour.free.fr/suexco.txt

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* For Sudoku, there are 9x9x9=729 possible choices (9 numbers to choose for
   each cell in a 9x9 grid), and 4x9x9=324 constraints with each constraint
   representing a set of choices that are mutually conflictive with each other.
   The 324 constraints are classified into 4 categories:

   1. row-column where each cell contains only one number
   2. box-number where each number appears only once in one 3x3 box
   3. row-number where each number appears only once in one row
   4. col-number where each number appears only once in one column

   Each category consists of 81 constraints. We number these constraints from 0
   to 323. In this program, for example, constraint 0 requires that the (0,0)
   cell contains only one number; constraint 81 requires that number 1 appears
   only once in the upper-left 3x3 box; constraint 162 requires that number 1
   appears only once in row 1; constraint 243 requires that number 1 appears
   only once in column 1.
   
   Noting that a constraint is a subset of choices, we may represent a
   constraint with a binary vector of 729 elements. Thus we have a 729x324
   binary matrix M with M(r,c)=1 indicating the constraint c involves choice r.
   Solving a Sudoku is reduced to finding a subset of choices such that no
   choices are present in the same constaint. This is equivalent to finding the
   minimal subset of choices intersecting all constraints, a minimum hitting
   set problem or a eqivalence of the exact cover problem.

   The 729x324 binary matrix is a sparse matrix, with each row containing 4
   non-zero elements and each column 9 non-zero elements. In practical
   implementation, we store the coordinate of non-zero elements instead of
   the binary matrix itself. We use a binary row vector to indicate the
   constraints that have not been used and use a column vector to keep the
   times a choice has been forbidden. When we set a choice, we will use up
   4 constraints and forbid other choices in the 4 constraints. When we make
`  wrong choices, we will find an unused constraint with all choices forbidden,
   in which case, we have to backtrack to make new choices. Once we understand
   what the 729x324 matrix represents, the backtracking algorithm itself is
   easy.
 */

// the sparse representation of the binary matrix
typedef struct {
	uint16_t r[324][9]; // M(r[c][i], c) is a non-zero element
	uint16_t c[729][4]; // M(r, c[r][j]) is a non-zero element
} sdaux_t;

// generate the sparse representation of the binary matrix
sdaux_t *sd_genmat()
{
	sdaux_t *a;
	int i, j, k, r, c, c2;
	int8_t nr[324];
	a = calloc(1, sizeof(sdaux_t));
	for (i = r = 0; i < 9; ++i) // generate c[729][4]
		for (j = 0; j < 9; ++j)
			for (k = 0; k < 9; ++k) // this "9" means each cell has 9 possible numbers
				a->c[r][0] = 9 * i + j,                  // row-column constraint
				a->c[r][1] = (i/3*3 + j/3) * 9 + k + 81, // box-number constraint
				a->c[r][2] = 9 * i + k + 162,            // row-number constraint
				a->c[r][3] = 9 * j + k + 243,            // col-number constraint
				++r;
	for (c = 0; c < 324; ++c) nr[c] = 0;
	for (r = 0; r < 729; ++r) // generate r[][] from c[][]
		for (c2 = 0; c2 < 4; ++c2)
			k = a->c[r][c2], a->r[k][nr[k]++] = r;
	return a;
}
// update the state vectors when we pick up choice r; v=1 for setting choice; v=-1 for reverting
inline void sd_update(const sdaux_t *aux, int16_t sr[729], int8_t sc[324], int r, int v)
{
	int c2;
	for (c2 = 0; c2 < 4; ++c2) {
		int r2, c = aux->c[r][c2];
		sc[c] += v;
		for (r2 = 0; r2 < 9; ++r2) sr[aux->r[c][r2]] += v; // 15% of CPU time
	}
}
// solve a Sudoku; _s is the standard dot/number representation
int sd_solve(const sdaux_t *aux, const char *_s)
{
	int i, j, r, c, r2, dir, c0, hints = 0; // dir=1: forward; dir=-1: backtrack
	int8_t sc[324], cr[81];
	int16_t cc[81], sr[729];
	char out[82];
	for (r = 0; r < 729; ++r) sr[r] = 0;
	for (c = 0; c < 324; ++c) sc[c] = 0;
	for (i = 0; i < 81; ++i) {
		int a = _s[i] >= '1' && _s[i] <= '9'? _s[i] - '1' : -1; // number from -1 to 8
		if (a >= 0) sd_update(aux, sr, sc, i * 9 + a, 1); // set the choice
		if (a >= 0) ++hints; // count the number of hints
		cr[i] = cc[i] = -1, out[i] = _s[i];
	}
	for (i = c0 = 0, dir = 1, out[81] = 0;;) {
		while (i >= 0 && i < 81 - hints) { // maximum 81-hints steps
			if (dir == 1) {
				int j, min = 10, n;
				for (j = 0; j < 324; ++j) { // 75% of CPU time goes to this block
					const uint16_t *p;
					c = j + c0 < 324? j + c0 : j + c0 - 324; // only explore cols not computed before
					if (sc[c]) continue; // skip if the constraint has been used
					for (r2 = n = 0, p = aux->r[c]; r2 < 9; ++r2)
						if (sr[p[r2]] == 0) ++n; // 30% of CPU time goes to this line
					if (n < min) min = n, cc[i] = c, c0 = c + 1; // choose the top constraint
					if (n <= 1) break; // this is for acceleration; slower without this line
				}
				if (min == 0 || min == 10) cr[i--] = dir = -1; // backtrack
			}
			c = cc[i];
			if (dir == -1 && cr[i] >= 0) sd_update(aux, sr, sc, aux->r[c][cr[i]], -1); // revert the choice
			for (r2 = cr[i] + 1; r2 < 9; ++r2) // search for the choice to make
				if (sr[aux->r[c][r2]] == 0) break; // found if the state equals 0
			if (r2 < 9) {
				sd_update(aux, sr, sc, aux->r[c][r2], 1); // set the choice
				cr[i++] = r2; dir = 1; // moving forward
			} else cr[i--] = dir = -1; // backtrack
		}
		if (i < 0) break;
		for (j = 0; j < i; ++j) r = aux->r[cc[j]][cr[j]], out[r/9] = r%9 + '1'; // print
		puts(out);
		--i; dir = -1; // backtrack
	}
	return 0;
}

int main()
{
	sdaux_t *a = sd_genmat();
	char buf[1024];
	while (fgets(buf, 1024, stdin) != 0) {
		if (strlen(buf) < 81) continue;
		sd_solve(a, buf);
		putchar('\n');
	}
	free(a);
	return 0;
}
