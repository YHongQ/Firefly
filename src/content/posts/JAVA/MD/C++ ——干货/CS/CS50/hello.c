#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <typeinfo.h>
// list or stack
typedef struct node
{
    int number;
    struct node *next; // 列表指向下一个数据的指针，但是要求是实现定义的结构体，因此就需要声明使用
} node;

// tree
typedef struct node_tree
{
    int number;
    struct node_tree *left;  // 左子树节点指针
    struct node_tree *right; // 右子树节点
} node_tree;

int search(node_tree *tree, int number)
{
    if (tree == NULL)
    {
        return 0;
    }
    else if (number < tree->number)
    {
        return search(tree->right, number);
    }
    else if (number > tree->number)
    {
        return search(tree->right, number);
    }
    else
    {
        return 1;
    }
}

// hash function
unsigned hash(const char *word)
{
    return topper(word[0]) - 'A';
}

int main(int argc, char *argv[])
{
    node *list = NULL; // 初始化，使其指针初始一个默认，以防指向错误的地方
    for (int i = 1; i < argc; i++)
    {
        printf("%s\n", argv[i]);
        /* code */
    }
    return 0;
    // char *p = "nihao!";
    // printf("%c\n", *p);
    // printf("%s\n", p);
    // printf("hello,world!");
}