// EXPECT: 339
// A comma-separated declarator list may mix array and scalar declarators
// under one shared base type, each picking its own shape/initializer, at
// both global and local (including 'static' local) scope.
int garr[2] = {1, 2}, gscal = 5;

int main() {
    int arr1[2] = {10, 20}, mid, arr2[2] = {100, 200};
    static int s1 = 1, s2;
    return garr[0] + garr[1] + gscal + arr1[0] + arr1[1] + mid + arr2[0] + arr2[1] + s1 + s2;
}
