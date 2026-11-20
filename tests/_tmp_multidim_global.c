// EXPECT: 138
int g[3][4];

int main() {
    int i;
    int j;
    int sum;
    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 4; j = j + 1) {
            g[i][j] = i * 10 + j;
        }
    }
    sum = 0;
    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 4; j = j + 1) {
            sum = sum + g[i][j];
        }
    }
    return sum;
}
