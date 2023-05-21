
#include <iostream> 
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <mutex>
#include <fstream>

#define STORE_FILE "store/dumpFile"

std::mutex mtx;     // 关键部分要上锁(插入数据删除数据的时候)
std::string delimiter = ":";

// Node的模板类
template<typename K, typename V> 
class Node {

public:
    
    Node() {} 

    Node(K k, V v, int); 

    ~Node();

    K get_key() const;

    V get_value() const;

    void set_value(V);
    
    //forward指针指向的是一个指针数组，数组里面存放的是Node<K,V>*类型的指针，这个线性数组用于保存该Node节点指向不同level的下一个节点的指针
    Node<K, V> **forward;

    //当前节点所在的level
    int node_level;

private:
    K key;
    V value;
};

template<typename K, typename V> 
Node<K, V>::Node(const K k, const V v, int level) {
    this->key = k;
    this->value = v;
    this->node_level = level; 

    // 是level + 1, 因为下标是从 0 到 level，有level + 1层
    this->forward = new Node<K, V>*[level+1];
    
	// 全部初始化为0
    memset(this->forward, 0, sizeof(Node<K, V>*)*(level+1));
};

template<typename K, typename V> 
Node<K, V>::~Node() {
    delete []forward;
};

template<typename K, typename V> 
K Node<K, V>::get_key() const {
    return key;
};

template<typename K, typename V> 
V Node<K, V>::get_value() const {
    return value;
};
template<typename K, typename V> 
void Node<K, V>::set_value(V value) {
    this->value=value;
};

//Skip list的类模板
template <typename K, typename V> 
class SkipList {

public: 
    SkipList(int);
    ~SkipList();
    int get_random_level();
    Node<K, V>* create_node(K, V, int);
    int insert_element(K, V);
    void display_list();
    bool search_element(K);
    void delete_element(K);
    void dump_file();
    void load_file();
    int size();

private:
    void get_key_value_from_string(const std::string& str, std::string* key, std::string* value);
    bool is_valid_string(const std::string& str);

private:    
    //跳表的最大level数
    int _max_level;

    //当前跳表的level
    int _skip_list_level;

    //指向头节点的指针
    Node<K, V> *_header;

    //文件操作
    std::ofstream _file_writer;//ofstream默认以输出方式打开文件，从内存到硬盘
    std::ifstream _file_reader;//ifstream默认以输入方式打开文件，从硬盘到内存

    // skiplist目前的元素数目
    int _element_count;
};
 
//创建一个新的节点
template<typename K, typename V>
Node<K, V>* SkipList<K, V>::create_node(const K k, const V v, int level) {
    Node<K, V> *n = new Node<K, V>(k, v, level);
    return n;
}

//向跳表中插入给定的元素key和value
//return 1代表元素已经存在
//return 0代表插入成功
/* 
                           +------------+
                           |  insert 50 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |                      insert +----+
level 3         1+-------->10+---------------> | 50 |          70       100
                                               |    |
                                               |    |
level 2         1          10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 1         1    4     10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 0         1    4   9 10         30   40  | 50 |  60      70       100
                                               +----+

*/
template<typename K, typename V>
int SkipList<K, V>::insert_element(const K key, const V value) {
    
    mtx.lock();
    Node<K, V> *current = this->_header;

    // 创建一个update数组并且初始化，0代表的就是尾节点 
    // update存放的node可以看作是一条路径，路径上的node是插入节点之前的节点，node->forward[i] 一会儿要被拿去操作，连接在插入节点后面
    Node<K, V> *update[_max_level+1];
    memset(update, 0, sizeof(Node<K, V>*)*(_max_level+1));  

    // 从当前跳表的最高层开始往下遍历 
    for(int i = _skip_list_level; i >= 0; i--) {
        while(current->forward[i] != NULL && current->forward[i]->get_key() < key) {
            current = current->forward[i]; //直到前面是null或者前面节点的值大于等于插入节点的值，才会转到下一层
        }
        update[i] = current;
    }

    // 此时到达最下面第0层，current指向要插入的位置
    current = current->forward[0];

    // 如果要插入位置的值恰好等于key，那说明之前已经存在了
    if (current != NULL && current->get_key() == key) {
        std::cout << "key: " << key << ", exists" << std::endl;
        mtx.unlock();
        return 1;
    }

    // 如果current等于NULL代表我们已经是到达了跳表最后的位置
    // 如果key的值不相等，说明我们要在此处新建一个值为key的node，然后插入update数组和update[i]->forward[i]之间 （和链表的插入是一样的道理，只不过这里有很多层链表，要从下往上依次连接）
    if (current == NULL || current->get_key() != key ) {
        
        // 给新建的节点创造一个random level
        // 注意这里random_level的生成也是有技巧的
        int random_level = get_random_level();

        // 如果random level比当前跳表的最大level还要大, 多出来的update数组就要初始化为header指针
        if (random_level > _skip_list_level) {
            for (int i = _skip_list_level+1; i < random_level+1; i++) {
                update[i] = _header;
            }
            _skip_list_level = random_level;
        }

        // 创建一个高为random_level的新节点
        Node<K, V>* inserted_node = create_node(key, value, random_level);
        
        // 插入节点，前后相连 
        for (int i = 0; i <= random_level; i++) {
            inserted_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = inserted_node;
        }
        std::cout << "Successfully inserted key:" << key << ", value:" << value << std::endl;
        _element_count ++;
    }
    mtx.unlock();
    return 0;
}

// 展示跳表 
template<typename K, typename V> 
void SkipList<K, V>::display_list() {

    std::cout << "\n*****Skip List*****"<<"\n"; 
    for (int i = 0; i <= _skip_list_level; i++) {
        Node<K, V> *node = this->_header->forward[i]; 
        std::cout << "Level " << i << ": ";
        while (node != NULL) {
            std::cout << node->get_key() << ":" << node->get_value() << ";";
            node = node->forward[i];
        }
        std::cout << std::endl;
    }
}

// 磁盘落地，把文件从内存转存的到磁盘文件中去
// 这一部分有待优化
template<typename K, typename V> 
void SkipList<K, V>::dump_file() {

    std::cout << "dump_file-----------------" << std::endl;
    _file_writer.open(STORE_FILE);
    Node<K, V> *node = this->_header->forward[0]; 

    while (node != NULL) {
        _file_writer << node->get_key() << ":" << node->get_value() << "\n";
        std::cout << node->get_key() << ":" << node->get_value() << ";\n";
        node = node->forward[0];
    }

    _file_writer.flush();//用于刷新输出流,将缓冲区中的数据立即发送到关联的输出设备（例如屏幕或文件）。
    _file_writer.close();
    return ;
}

// 从磁盘中加载数据
// 亦有待优化
template<typename K, typename V> 
void SkipList<K, V>::load_file() {

    _file_reader.open(STORE_FILE);
    std::cout << "load_file-----------------" << std::endl;
    std::string line;
    std::string* key = new std::string();
    std::string* value = new std::string();
    while (getline(_file_reader, line)) {
        get_key_value_from_string(line, key, value);
        if (key->empty() || value->empty()) {
            continue;
        }
        insert_element(*key, *value);
        std::cout << "key:" << *key << "value:" << *value << std::endl;
    }
    _file_reader.close();
}

// 获取跳表当前的元素数目 
template<typename K, typename V> 
int SkipList<K, V>::size() { 
    return _element_count;
}

template<typename K, typename V>
void SkipList<K, V>::get_key_value_from_string(const std::string& str, std::string* key, std::string* value) {

    if(!is_valid_string(str)) {//先判断是不是有效的字符串
        return;
    }
    *key = str.substr(0, str.find(delimiter));
    *value = str.substr(str.find(delimiter)+1, str.length());
}

template<typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string& str) {

    if (str.empty()) {
        return false;
    }
    if (str.find(delimiter) == std::string::npos) {
        return false;
    }
    return true;
}

// 从跳表中删除元素
template<typename K, typename V> 
void SkipList<K, V>::delete_element(K key) {

    mtx.lock();
    Node<K, V> *current = this->_header; 
    Node<K, V> *update[_max_level+1];
    memset(update, 0, sizeof(Node<K, V>*)*(_max_level+1));

    // 从当前跳表最高层开始
    for (int i = _skip_list_level; i >= 0; i--) {
        while (current->forward[i] !=NULL && current->forward[i]->get_key() < key) {
            current = current->forward[i];
        }
        update[i] = current;// update数组记录的是每层里待删除结点前面的那个节点
    }

    current = current->forward[0];
    if (current != NULL && current->get_key() == key) {
       
        // 从最低层开始，把每层的待删除结点都删除
        for (int i = 0; i <= _skip_list_level; i++) {

            // 如果在第i层, update[i]的下一个节点不是待删除节点了，就跳出循环
            if (update[i]->forward[i] != current) 
                break;

            update[i]->forward[i] = current->forward[i];
        }

        // _header->forward[_skip_list_level] == 0代表头尾指针相连，中间没有元素了，这一层就不必存在了
        while (_skip_list_level > 0 && _header->forward[_skip_list_level] == 0) {
            _skip_list_level --; 
        }

        std::cout << "Successfully deleted key "<< key << std::endl;
        _element_count --;
    }
    mtx.unlock();
    return;
}

// 在跳表中搜寻元素
/*
                           +------------+
                           |  select 60 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |
level 3         1+-------->10+------------------>50+           70       100
                                                   |
                                                   |
level 2         1          10         30         50|           70       100
                                                   |
                                                   |
level 1         1    4     10         30         50|           70       100
                                                   |
                                                   |
level 0         1    4   9 10         30   40    50+-->60      70       100
*/
template<typename K, typename V> 
bool SkipList<K, V>::search_element(K key) {

    std::cout << "search_element-----------------" << std::endl;
    Node<K, V> *current = _header;

    // 从最高的level开始
    for (int i = _skip_list_level; i >= 0; i--) {
        while (current->forward[i] && current->forward[i]->get_key() < key) {
            current = current->forward[i];
        }
    }

    //到达第0层，前面的元素就应该是我们要找的元素
    current = current->forward[0];

    // 如果恰好相等，说明找到了
    if (current and current->get_key() == key) {
        std::cout << "Found key: " << key << ", value: " << current->get_value() << std::endl;
        return true;
    }

    std::cout << "Not Found Key:" << key << std::endl;
    return false;
}

// 构造跳表
template<typename K, typename V> 
SkipList<K, V>::SkipList(int max_level) {

    this->_max_level = max_level;
    this->_skip_list_level = 0;
    this->_element_count = 0;

    // 创建头节点并初始化
    K k;
    V v;
    this->_header = new Node<K, V>(k, v, _max_level);
};

template<typename K, typename V> 
SkipList<K, V>::~SkipList() {

    if (_file_writer.is_open()) {
        _file_writer.close();
    }
    if (_file_reader.is_open()) {
        _file_reader.close();
    }
    delete _header;
}

template<typename K, typename V>
int SkipList<K, V>::get_random_level(){

    int k = 1;
    // 这里的设计思路是这样的：让越大的k出现的概率越小，因为我们希望跳表中大的level还是少一点比较好，如果所有node的level都偏高，那和普通链表查询就类似了，查询速率肯定就低于logn了
    // 如果仅仅是 k = rand() % _max_level，那么在0和_max_level之间的每个level是等概率出现的，但是实际上，小的level出现的概率应该大于大的level
    while (rand() % 2) {
        k++;
    }
    k = (k < _max_level) ? k : _max_level;//k最大不能超过_max_level
    return k;
};
